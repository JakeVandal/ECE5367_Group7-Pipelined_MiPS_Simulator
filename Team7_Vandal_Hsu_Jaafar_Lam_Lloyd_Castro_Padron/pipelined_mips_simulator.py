#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

NUM_REGS = 32
MEM_SIZE_WORDS = 4096

REGISTER_MAP = {
    '$zero': 0, '$0': 0,
    '$at': 1,
    '$v0': 2, '$v1': 3,
    '$a0': 4, '$a1': 5, '$a2': 6, '$a3': 7,
    '$t0': 8, '$t1': 9, '$t2': 10, '$t3': 11, '$t4': 12, '$t5': 13, '$t6': 14, '$t7': 15,
    '$s0': 16, '$s1': 17, '$s2': 18, '$s3': 19, '$s4': 20, '$s5': 21, '$s6': 22, '$s7': 23,
    '$t8': 24, '$t9': 25,
    '$k0': 26, '$k1': 27,
    '$gp': 28, '$sp': 29, '$fp': 30, '$ra': 31,
}

REG_NUM_TO_NAME = {v: k for k, v in REGISTER_MAP.items() if not k.startswith('$0')}
for i in range(NUM_REGS):
    REG_NUM_TO_NAME.setdefault(i, f'${i}')

R_FUNCTS = {
    0x20: 'add',
    0x22: 'sub',
    0x24: 'and',
    0x25: 'or',
    0x2A: 'slt',
}

I_OPS = {
    0x08: 'addi',
    0x23: 'lw',
    0x2B: 'sw',
    0x04: 'beq',
}

J_OPS = {
    0x02: 'j',
}


@dataclass
class Instruction:
    name: str = 'nop'
    rs: int = 0
    rt: int = 0
    rd: int = 0
    imm: int = 0
    address: int = 0
    raw: Optional[int] = None
    asm: str = 'nop'

    def is_nop(self) -> bool:
        return self.name == 'nop'

    def dest_reg(self) -> int:
        if self.name in {'add', 'sub', 'and', 'or', 'slt'}:
            return self.rd
        if self.name in {'addi', 'lw'}:
            return self.rt
        return 0

    def writes_reg(self) -> bool:
        return self.dest_reg() != 0

    def reads_rs(self) -> bool:
        return self.name in {'add', 'sub', 'and', 'or', 'slt', 'addi', 'lw', 'sw', 'beq'}

    def reads_rt(self) -> bool:
        return self.name in {'add', 'sub', 'and', 'or', 'slt', 'sw', 'beq'}


@dataclass
class IFID:
    instr: Instruction = field(default_factory=Instruction)
    pc: int = 0


@dataclass
class IDEX:
    instr: Instruction = field(default_factory=Instruction)
    pc: int = 0
    rs_val: int = 0
    rt_val: int = 0


@dataclass
class EXMEM:
    instr: Instruction = field(default_factory=Instruction)
    pc: int = 0
    alu_result: int = 0
    rt_val: int = 0
    dest_reg: int = 0
    zero: bool = False
    branch_target: int = 0
    take_branch: bool = False


@dataclass
class MEMWB:
    instr: Instruction = field(default_factory=Instruction)
    pc: int = 0
    mem_data: int = 0
    alu_result: int = 0
    dest_reg: int = 0


def to_signed32(x: int) -> int:
    x &= 0xFFFFFFFF
    return x if x < 0x80000000 else x - 0x100000000


def parse_reg(token: str) -> int:
    token = token.strip().lower()
    if token not in REGISTER_MAP:
        raise ValueError(f'Unknown register: {token}')
    return REGISTER_MAP[token]


def clean_line(line: str) -> str:
    line = line.split('#', 1)[0]
    return line.strip()


def format_instr(instr: Instruction) -> str:
    return instr.asm if instr.asm else instr.name


def decode_word(word: int, pc: int) -> Instruction:
    if word == 0:
        return Instruction(name='nop', raw=word, address=pc, asm='nop')

    opcode = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    funct = word & 0x3F
    imm = to_signed32(word & 0xFFFF)
    if imm > 0x7FFF:
        imm -= 0x10000
    address = word & 0x03FFFFFF

    if opcode == 0:
        name = R_FUNCTS.get(funct)
        if name is None:
            raise ValueError(f'Unsupported R-type funct 0x{funct:02X}')
        asm = f'{name} {REG_NUM_TO_NAME[rd]}, {REG_NUM_TO_NAME[rs]}, {REG_NUM_TO_NAME[rt]}'
        return Instruction(name=name, rs=rs, rt=rt, rd=rd, address=pc, raw=word, asm=asm)
    if opcode in I_OPS:
        name = I_OPS[opcode]
        if name in {'lw', 'sw'}:
            asm = f'{name} {REG_NUM_TO_NAME[rt]}, {imm}({REG_NUM_TO_NAME[rs]})'
        elif name == 'beq':
            asm = f'beq {REG_NUM_TO_NAME[rs]}, {REG_NUM_TO_NAME[rt]}, {imm}'
        else:
            asm = f'addi {REG_NUM_TO_NAME[rt]}, {REG_NUM_TO_NAME[rs]}, {imm}'
        return Instruction(name=name, rs=rs, rt=rt, imm=imm, address=pc, raw=word, asm=asm)
    if opcode in J_OPS:
        name = J_OPS[opcode]
        target = ((pc + 4) & 0xF0000000) | (address << 2)
        asm = f'j 0x{target:08X}'
        return Instruction(name=name, address=target, raw=word, asm=asm)

    raise ValueError(f'Unsupported opcode 0x{opcode:02X}')


class AsmParser:
    def __init__(self, text: str):
        self.lines = text.splitlines()
        self.labels: Dict[str, int] = {}
        self.instructions: List[Instruction] = []

    def parse(self) -> List[Instruction]:
        self._first_pass()
        self._second_pass()
        return self.instructions

    def _first_pass(self) -> None:
        pc = 0
        for raw_line in self.lines:
            line = clean_line(raw_line)
            if not line:
                continue
            while ':' in line:
                label, rest = line.split(':', 1)
                self.labels[label.strip()] = pc
                line = rest.strip()
                if not line:
                    break
            if line:
                pc += 4

    def _second_pass(self) -> None:
        pc = 0
        for raw_line in self.lines:
            line = clean_line(raw_line)
            if not line:
                continue
            while ':' in line:
                _, rest = line.split(':', 1)
                line = rest.strip()
                if not line:
                    break
            if not line:
                continue
            instr = self._parse_instruction(line, pc)
            self.instructions.append(instr)
            pc += 4

    def _parse_instruction(self, line: str, pc: int) -> Instruction:
        tokens = re.split(r'[\s,]+', line)
        op = tokens[0].lower()

        if op == 'nop':
            return Instruction(name='nop', address=pc, asm='nop')
        if op in {'add', 'sub', 'and', 'or', 'slt'}:
            rd, rs, rt = map(parse_reg, tokens[1:4])
            return Instruction(name=op, rd=rd, rs=rs, rt=rt, address=pc, asm=line)
        if op == 'addi':
            rt, rs = map(parse_reg, tokens[1:3])
            imm = int(tokens[3], 0)
            return Instruction(name=op, rt=rt, rs=rs, imm=imm, address=pc, asm=line)
        if op in {'lw', 'sw'}:
            rt = parse_reg(tokens[1])
            match = re.match(r'(-?0x[0-9a-fA-F]+|-?\d+)\(([^)]+)\)$', tokens[2])
            if not match:
                raise ValueError(f'Bad memory operand: {tokens[2]}')
            imm = int(match.group(1), 0)
            rs = parse_reg(match.group(2))
            return Instruction(name=op, rt=rt, rs=rs, imm=imm, address=pc, asm=line)
        if op == 'beq':
            rs, rt = map(parse_reg, tokens[1:3])
            label = tokens[3]
            if label not in self.labels:
                raise ValueError(f'Unknown label: {label}')
            target_pc = self.labels[label]
            offset = (target_pc - (pc + 4)) // 4
            return Instruction(name=op, rs=rs, rt=rt, imm=offset, address=pc, asm=line)
        if op == 'j':
            label = tokens[1]
            if label in self.labels:
                target = self.labels[label]
            else:
                target = int(label, 0)
            return Instruction(name='j', address=target, asm=line)
        raise ValueError(f'Unsupported assembly op: {op}')


class PipelinedMIPSSimulator:
    def __init__(self, instructions: List[Instruction], data_mem: Optional[Dict[int, int]] = None):
        self.program = instructions
        self.program_map = {instr.address: instr for instr in instructions}
        self.reg = [0] * NUM_REGS
        self.mem = [0] * MEM_SIZE_WORDS
        if data_mem:
            for addr, value in data_mem.items():
                self.store_word(addr, value)

        self.pc = 0
        self.cycle = 0
        self.if_id = IFID()
        self.id_ex = IDEX()
        self.ex_mem = EXMEM()
        self.mem_wb = MEMWB()
        self.history: List[dict] = []
        self.halted = False

    def load_word(self, addr: int) -> int:
        if addr % 4 != 0:
            raise ValueError(f'Unaligned load address {addr}')
        idx = addr // 4
        if not (0 <= idx < len(self.mem)):
            raise ValueError(f'Load address out of range {addr}')
        return self.mem[idx]

    def store_word(self, addr: int, value: int) -> None:
        if addr % 4 != 0:
            raise ValueError(f'Unaligned store address {addr}')
        idx = addr // 4
        if not (0 <= idx < len(self.mem)):
            raise ValueError(f'Store address out of range {addr}')
        self.mem[idx] = to_signed32(value)

    def fetch_instr(self, pc: int) -> Instruction:
        return self.program_map.get(pc, Instruction(name='nop', address=pc, asm='nop'))

    def pipeline_empty(self) -> bool:
        return (self.if_id.instr.is_nop() and self.id_ex.instr.is_nop() and
                self.ex_mem.instr.is_nop() and self.mem_wb.instr.is_nop())

    def detect_load_use_hazard(self) -> bool:
        exi = self.id_ex.instr
        idi = self.if_id.instr
        if exi.name != 'lw':
            return False
        load_dest = exi.rt
        if load_dest == 0:
            return False
        return ((idi.reads_rs() and idi.rs == load_dest) or
                (idi.reads_rt() and idi.rt == load_dest))

    def forwarding_value(self, reg_num: int, original: int, operand: str) -> Tuple[int, str]:
        if reg_num == 0:
            return 0, 'ZERO'

        # EX/MEM forwarding only for ALU-producing instructions, not lw.
        exi = self.ex_mem.instr
        if exi.writes_reg() and exi.dest_reg() == reg_num and exi.name != 'lw':
            return self.ex_mem.alu_result, 'EX/MEM'

        # MEM/WB forwarding can use either memory data or ALU result.
        memi = self.mem_wb.instr
        if memi.writes_reg() and memi.dest_reg() == reg_num:
            if memi.name == 'lw':
                return self.mem_wb.mem_data, 'MEM/WB'
            return self.mem_wb.alu_result, 'MEM/WB'

        return original, 'ID/EX'

    def step(self) -> None:
        if self.halted:
            return
        self.cycle += 1

        old_if_id = IFID(self.if_id.instr, self.if_id.pc)
        old_id_ex = IDEX(self.id_ex.instr, self.id_ex.pc, self.id_ex.rs_val, self.id_ex.rt_val)
        old_ex_mem = EXMEM(self.ex_mem.instr, self.ex_mem.pc, self.ex_mem.alu_result,
                           self.ex_mem.rt_val, self.ex_mem.dest_reg, self.ex_mem.zero,
                           self.ex_mem.branch_target, self.ex_mem.take_branch)
        old_mem_wb = MEMWB(self.mem_wb.instr, self.mem_wb.pc, self.mem_wb.mem_data,
                           self.mem_wb.alu_result, self.mem_wb.dest_reg)

        trace = {
            'cycle': self.cycle,
            'stall': False,
            'flush_ifid': False,
            'flush_idex': False,
            'taken': False,
            'forwardA': 'ID/EX',
            'forwardB': 'ID/EX',
            'wb_write': None,
            'mem_write': None,
            'mem_read': None,
        }

        # ---------------- WB ----------------
        wbi = old_mem_wb.instr
        if wbi.writes_reg():
            wb_value = old_mem_wb.mem_data if wbi.name == 'lw' else old_mem_wb.alu_result
            if old_mem_wb.dest_reg != 0:
                self.reg[old_mem_wb.dest_reg] = to_signed32(wb_value)
                trace['wb_write'] = (old_mem_wb.dest_reg, to_signed32(wb_value))
        self.reg[0] = 0

        # ---------------- MEM ----------------
        next_mem_wb = MEMWB(instr=old_ex_mem.instr, pc=old_ex_mem.pc,
                            alu_result=old_ex_mem.alu_result, dest_reg=old_ex_mem.dest_reg)
        memi = old_ex_mem.instr
        if memi.name == 'lw':
            next_mem_wb.mem_data = self.load_word(old_ex_mem.alu_result)
            trace['mem_read'] = (old_ex_mem.alu_result, next_mem_wb.mem_data)
        elif memi.name == 'sw':
            self.store_word(old_ex_mem.alu_result, old_ex_mem.rt_val)
            trace['mem_write'] = (old_ex_mem.alu_result, old_ex_mem.rt_val)

        # ---------------- EX ----------------
        exi = old_id_ex.instr
        a_val, fwd_a = self.forwarding_value(exi.rs, old_id_ex.rs_val, 'A')
        b_val, fwd_b = self.forwarding_value(exi.rt, old_id_ex.rt_val, 'B')
        trace['forwardA'] = fwd_a
        trace['forwardB'] = fwd_b

        next_ex_mem = EXMEM(instr=exi, pc=old_id_ex.pc, rt_val=b_val, dest_reg=exi.dest_reg())
        if exi.name == 'add':
            next_ex_mem.alu_result = to_signed32(a_val + b_val)
        elif exi.name == 'sub':
            next_ex_mem.alu_result = to_signed32(a_val - b_val)
        elif exi.name == 'and':
            next_ex_mem.alu_result = a_val & b_val
        elif exi.name == 'or':
            next_ex_mem.alu_result = a_val | b_val
        elif exi.name == 'slt':
            next_ex_mem.alu_result = 1 if a_val < b_val else 0
        elif exi.name == 'addi':
            next_ex_mem.alu_result = to_signed32(a_val + exi.imm)
        elif exi.name in {'lw', 'sw'}:
            next_ex_mem.alu_result = to_signed32(a_val + exi.imm)
        elif exi.name == 'beq':
            next_ex_mem.zero = (a_val == b_val)
            next_ex_mem.take_branch = next_ex_mem.zero
            next_ex_mem.branch_target = old_id_ex.pc + 4 + (exi.imm << 2)
        elif exi.name == 'j':
            next_ex_mem.take_branch = True
            next_ex_mem.branch_target = exi.address

        # ---------------- ID / hazard ----------------
        stall = self.detect_load_use_hazard()
        trace['stall'] = stall
        if stall:
            next_id_ex = IDEX(instr=Instruction())
        else:
            idi = old_if_id.instr
            next_id_ex = IDEX(instr=idi, pc=old_if_id.pc,
                              rs_val=self.reg[idi.rs] if idi.reads_rs() else 0,
                              rt_val=self.reg[idi.rt] if idi.reads_rt() else 0)

        # ---------------- control transfer ----------------
        next_pc = self.pc
        next_if_id = IFID(old_if_id.instr, old_if_id.pc)
        if next_ex_mem.instr.name in {'beq', 'j'} and next_ex_mem.take_branch:
            trace['taken'] = True
            trace['flush_ifid'] = True
            trace['flush_idex'] = True
            next_pc = next_ex_mem.branch_target
            next_if_id = IFID(instr=Instruction(), pc=0)
            next_id_ex = IDEX(instr=Instruction())
        elif stall:
            next_pc = self.pc
            next_if_id = old_if_id
        else:
            fetched = self.fetch_instr(self.pc)
            next_if_id = IFID(instr=fetched, pc=self.pc)
            next_pc = self.pc + 4

        trace['next_pc'] = next_pc

        # Commit pipeline registers
        self.mem_wb = next_mem_wb
        self.ex_mem = next_ex_mem
        self.id_ex = next_id_ex
        self.if_id = next_if_id
        self.pc = next_pc
        self.reg[0] = 0

        # Store stage snapshot after update for display.
        trace['IF'] = self.if_id.instr
        trace['ID'] = self.id_ex.instr
        trace['EX'] = self.ex_mem.instr
        trace['MEM'] = self.mem_wb.instr
        trace['WB'] = old_mem_wb.instr
        self.history.append(trace)

        # Halt when PC has moved beyond program and pipeline drained.
        if self.pc >= len(self.program) * 4 and self.pipeline_empty():
            self.halted = True

    def run(self, max_cycles: int = 100) -> None:
        while not self.halted and self.cycle < max_cycles:
            self.step()

    def render_cycle_log(self) -> str:
        lines: List[str] = []
        for h in self.history:
            lines.append(f"Cycle {h['cycle']}")
            lines.append(f"  IF : {format_instr(h['IF'])}")
            lines.append(f"  ID : {format_instr(h['ID'])}")
            lines.append(f"  EX : {format_instr(h['EX'])}")
            lines.append(f"  MEM: {format_instr(h['MEM'])}")
            lines.append(f"  WB : {format_instr(h['WB'])}")
            lines.append(
                f"  stall={h['stall']} flush_ifid={h['flush_ifid']} flush_idex={h['flush_idex']} taken={h['taken']}")
            lines.append(f"  forwardA={h['forwardA']} forwardB={h['forwardB']}")
            lines.append(f"  next_pc=0x{h['next_pc']:08X}")
            if h['wb_write']:
                reg_num, value = h['wb_write']
                lines.append(f"  wb_write: {REG_NUM_TO_NAME[reg_num]} = {value}")
            if h['mem_read']:
                addr, value = h['mem_read']
                lines.append(f"  mem_read:  mem[0x{addr:08X}] = {value}")
            if h['mem_write']:
                addr, value = h['mem_write']
                lines.append(f"  mem_write: mem[0x{addr:08X}] = {value}")
            lines.append('')
        return '\n'.join(lines)


    def render_html(self) -> str:
        def esc(s: str) -> str:
            return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))

        css = """
        body { font-family: Arial, sans-serif; margin: 24px; background: #f7f7fb; color: #222; }
        h1, h2 { margin-bottom: 8px; }
        table { border-collapse: collapse; width: 100%; margin: 12px 0 24px; background: white; }
        th, td { border: 1px solid #d0d4e0; padding: 8px 10px; text-align: center; }
        th:first-child, td:first-child { text-align: left; }
        .IF { background: #dbeafe; }
        .ID { background: #e9d5ff; }
        .EX { background: #fde68a; }
        .MEM { background: #bbf7d0; }
        .WB { background: #fecaca; }
        .meta { font-family: Consolas, monospace; white-space: pre-wrap; background: white; border: 1px solid #d0d4e0; padding: 12px; }
        .flag-true { color: #b91c1c; font-weight: bold; }
        .flag-false { color: #475569; }
        """
        parts = [f"<html><head><meta charset='utf-8'><title>MIPS Pipeline Report</title><style>{css}</style></head><body>"]
        parts.append('<h1>MIPS Pipeline Visualization</h1>')
        parts.append('<h2>Instruction vs Cycle Grid</h2>')
        ordered_instrs = []
        instr_rows = {}
        for h in self.history:
            for stage in ['IF', 'ID', 'EX', 'MEM', 'WB']:
                instr = h[stage]
                if instr.is_nop():
                    continue
                key = f"0x{instr.address:08X}  {format_instr(instr)}"
                if key not in instr_rows:
                    instr_rows[key] = [''] * self.cycle
                    ordered_instrs.append(key)
                instr_rows[key][h['cycle'] - 1] = stage
        parts.append("<table><tr><th>Instruction</th>" + ''.join(f"<th>C{c}</th>" for c in range(1, self.cycle + 1)) + "</tr>")
        for key in ordered_instrs:
            parts.append(f"<tr><td>{esc(key)}</td>" + ''.join(f"<td class='{stage}'>{stage}</td>" if stage else '<td></td>' for stage in instr_rows[key]) + "</tr>")
        parts.append('</table>')
        parts.append('<h2>Per-cycle Stage Snapshot</h2>')
        parts.append("<table><tr><th>Cycle</th><th>IF</th><th>ID</th><th>EX</th><th>MEM</th><th>WB</th><th>Flags</th><th>Forwarding</th><th>Next PC</th></tr>")
        for h in self.history:
            flags = ' '.join([
                f"stall=<span class='flag-{'true' if h['stall'] else 'false'}'>{h['stall']}</span>",
                f"flush_ifid=<span class='flag-{'true' if h['flush_ifid'] else 'false'}'>{h['flush_ifid']}</span>",
                f"flush_idex=<span class='flag-{'true' if h['flush_idex'] else 'false'}'>{h['flush_idex']}</span>",
                f"taken=<span class='flag-{'true' if h['taken'] else 'false'}'>{h['taken']}</span>",
            ])
            parts.append(
                '<tr>'
                f"<td>{h['cycle']}</td>"
                f"<td class='IF'>{esc(format_instr(h['IF']))}</td>"
                f"<td class='ID'>{esc(format_instr(h['ID']))}</td>"
                f"<td class='EX'>{esc(format_instr(h['EX']))}</td>"
                f"<td class='MEM'>{esc(format_instr(h['MEM']))}</td>"
                f"<td class='WB'>{esc(format_instr(h['WB']))}</td>"
                f"<td>{flags}</td>"
                f"<td>A={esc(h['forwardA'])}, B={esc(h['forwardB'])}</td>"
                f"<td>0x{h['next_pc']:08X}</td>"
                '</tr>'
            )
        parts.append('</table>')
        parts.append('<h2>Final Registers</h2>')
        reg_lines = [f"{REG_NUM_TO_NAME[i]} = {v}" for i, v in enumerate(self.reg) if v != 0]
        parts.append(f"<div class='meta'>{esc(chr(10).join(reg_lines) if reg_lines else 'All registers are zero.')}</div>")
        parts.append('</body></html>')
        return ''.join(parts)

    def render_pipeline_grid(self) -> str:
        rows = []
        header = ['Instruction'] + [f'C{c}' for c in range(1, self.cycle + 1)]
        instr_rows: Dict[str, List[str]] = {}
        ordered_instrs: List[str] = []
        for h in self.history:
            for stage in ['IF', 'ID', 'EX', 'MEM', 'WB']:
                instr = h[stage]
                if instr.is_nop():
                    continue
                key = f"0x{instr.address:08X}  {format_instr(instr)}"
                if key not in instr_rows:
                    instr_rows[key] = [''] * self.cycle
                    ordered_instrs.append(key)
                instr_rows[key][h['cycle'] - 1] = stage
        widths = [max(len(header[0]), *(len(k) for k in ordered_instrs))] + [4] * self.cycle
        def fmt_row(cols: List[str]) -> str:
            return ' | '.join(col.ljust(widths[i]) for i, col in enumerate(cols))
        rows.append(fmt_row(header))
        rows.append('-+-'.join('-' * w for w in widths))
        for key in ordered_instrs:
            rows.append(fmt_row([key] + instr_rows[key]))
        return '\n'.join(rows)


def parse_machine_file(path: Path) -> List[Instruction]:
    instructions: List[Instruction] = []
    pc = 0
    for raw_line in path.read_text().splitlines():
        line = clean_line(raw_line).replace('_', '')
        if not line:
            continue
        line = line.lower().removeprefix('0x')
        word = int(line, 16)
        instructions.append(decode_word(word, pc))
        pc += 4
    return instructions


def parse_asm_file(path: Path) -> List[Instruction]:
    return AsmParser(path.read_text()).parse()


def infer_data_memory(program: List[Instruction]) -> Dict[int, int]:
    data = {}
    # Seed a few words so lw/sw tests visibly do something.
    for addr, value in [(0, 10), (4, 20), (8, 0), (12, 7), (16, 3), (20, 99), (24, 42), (28, -5), (32, 8)]:
        data[addr] = value
    return data


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description='Pipelined MIPS simulator with pipeline stage visualization.')
    p.add_argument('input_file', help='Assembly (.asm) or machine-code hex file')
    p.add_argument('--mode', choices=['auto', 'asm', 'machine'], default='auto')
    p.add_argument('--cycles', type=int, default=40, help='Maximum cycles to run')
    p.add_argument('--grid', action='store_true', help='Also print an instruction-vs-cycle pipeline grid')
    p.add_argument('--out', help='Write output to a text file')
    p.add_argument('--html', help='Write an HTML pipeline visualization report')
    return p


def detect_mode(path: Path) -> str:
    sample = path.read_text(errors='ignore')
    stripped = '\n'.join(clean_line(line) for line in sample.splitlines() if clean_line(line))
    if re.search(r'^(0x)?[0-9a-fA-F]{8}$', stripped.splitlines()[0] if stripped.splitlines() else ''):
        return 'machine'
    return 'asm'


def main() -> None:
    args = build_arg_parser().parse_args()
    path = Path(args.input_file)
    mode = detect_mode(path) if args.mode == 'auto' else args.mode

    if mode == 'machine':
        program = parse_machine_file(path)
    else:
        program = parse_asm_file(path)

    sim = PipelinedMIPSSimulator(program, infer_data_memory(program))
    sim.run(args.cycles)

    sections = [
        f'Processed file: {path.name}',
        f'Parsed mode: {mode}',
        '',
        '=== Cycle-by-cycle pipeline view ===',
        sim.render_cycle_log(),
        '=== Final registers (non-zero only) ===',
    ]
    for i, value in enumerate(sim.reg):
        if value != 0:
            sections.append(f'  {REG_NUM_TO_NAME[i]} = {value}')
    sections.append('')
    sections.append('=== Final memory snapshot (first 10 words) ===')
    for addr in range(0, 40, 4):
        sections.append(f'  mem[0x{addr:08X}] = {sim.load_word(addr)}')
    if args.grid:
        sections += ['', '=== Pipeline grid ===', sim.render_pipeline_grid()]

    output = '\n'.join(sections)
    print(output)
    if args.out:
        Path(args.out).write_text(output)
    if args.html:
        Path(args.html).write_text(sim.render_html())


if __name__ == '__main__':
    main()
