# FINAL PROJECT
# Group 7
# ECE 5367
# Jake Vandal, Nicholas Lloyd, Kenny Lam, Alejandro Castro, Hind Jaafar, Preston Hsu, Sebastian Padron
# Pipelined MIPS Simulator

from enum import Enum, auto

# ================= CONSTANTS =================

NUM_REGS = 32
MEM_SIZE = 1024
MAX_INSTR = 100

# ================= INSTRUCTION =================

class OpCode(Enum):
    ADD = auto()
    SUB = auto()
    LW  = auto()
    SW  = auto()
    NOP = auto()

class Instruction:
    def __init__(self, op=OpCode.NOP, rs=0, rt=0, rd=0, imm=0):
        self.op = op
        self.rs = rs
        self.rt = rt
        self.rd = rd
        self.imm = imm

# ================= PIPELINE REGISTERS =================

class IF_ID:
    def __init__(self):
        self.instr = Instruction()
        self.pc = 0

class ID_EX:
    def __init__(self):
        self.instr = Instruction()
        self.rs_val = 0
        self.rt_val = 0

class EX_MEM:
    def __init__(self):
        self.instr = Instruction()
        self.alu_result = 0
        self.rt_val = 0

class MEM_WB:
    def __init__(self):
        self.instr = Instruction()
        self.mem_data = 0
        self.alu_result = 0

# ================= GLOBAL STATE =================

REG = [0] * NUM_REGS
MEM = [0] * MEM_SIZE
instr_mem = []

if_id = IF_ID()
id_ex = ID_EX()
ex_mem = EX_MEM()
mem_wb = MEM_WB()

pc = 0
stall = False

# ================= UTILITY =================

def print_instr(instr):
    print(instr.op.name, end="")

# ================= HAZARD DETECTION =================

def detect_hazard(id_instr, ex_instr, mem_instr):
    def check(instr):
        if instr.op == OpCode.LW:
            dest = instr.rt
        else:
            dest = instr.rd
        return dest != 0 and (id_instr.rs == dest or id_instr.rt == dest)

    if ex_instr.op in (OpCode.ADD, OpCode.SUB, OpCode.LW):
        if check(ex_instr):
            return True

    if mem_instr.op in (OpCode.ADD, OpCode.SUB, OpCode.LW):
        if check(mem_instr):
            return True

    return False

# ================= PIPELINE STAGES =================

def WB_stage():
    instr = mem_wb.instr
    if instr.op in (OpCode.ADD, OpCode.SUB):
        REG[instr.rd] = mem_wb.alu_result
    elif instr.op == OpCode.LW:
        REG[instr.rt] = mem_wb.mem_data

def MEM_stage():
    mem_wb.instr = ex_mem.instr
    mem_wb.alu_result = ex_mem.alu_result

    if ex_mem.instr.op == OpCode.LW:
        mem_wb.mem_data = MEM[ex_mem.alu_result]
    elif ex_mem.instr.op == OpCode.SW:
        MEM[ex_mem.alu_result] = ex_mem.rt_val

def EX_stage():
    ex_mem.instr = id_ex.instr

    if id_ex.instr.op == OpCode.ADD:
        ex_mem.alu_result = id_ex.rs_val + id_ex.rt_val
    elif id_ex.instr.op == OpCode.SUB:
        ex_mem.alu_result = id_ex.rs_val - id_ex.rt_val
    elif id_ex.instr.op in (OpCode.LW, OpCode.SW):
        ex_mem.alu_result = id_ex.rs_val + id_ex.instr.imm
        ex_mem.rt_val = id_ex.rt_val

def ID_stage():
    global stall
    instr = if_id.instr

    if detect_hazard(instr, id_ex.instr, ex_mem.instr):
        stall = True
        id_ex.instr = Instruction(OpCode.NOP)
        return

    stall = False
    id_ex.instr = instr
    id_ex.rs_val = REG[instr.rs]
    id_ex.rt_val = REG[instr.rt]

def IF_stage():
    global pc
    if stall:
        return

    if pc < len(instr_mem):
        if_id.instr = instr_mem[pc]
        pc += 1
    else:
        if_id.instr = Instruction(OpCode.NOP)

# ================= SIMULATION =================

def run_pipeline(cycles):
    for cycle in range(cycles):
        print(f"\nCycle {cycle + 1}")

        WB_stage()
        MEM_stage()
        EX_stage()
        ID_stage()
        IF_stage()

        print("IF:  ", end=""); print_instr(if_id.instr); print()
        print("ID:  ", end=""); print_instr(id_ex.instr); print()
        print("EX:  ", end=""); print_instr(ex_mem.instr); print()
        print("MEM: ", end=""); print_instr(mem_wb.instr); print()

# ================= TEST PROGRAM =================

def load_test_program():
    global instr_mem
    instr_mem = [
        Instruction(OpCode.ADD, 1, 2, 3, 0),    # R3 = R1 + R2
        Instruction(OpCode.SUB, 3, 1, 4, 0),    # R4 = R3 - R1
        Instruction(OpCode.LW,  1, 5, 0, 10),   # R5 = MEM[R1 + 10]
        Instruction(OpCode.SW,  2, 5, 0, 20),   # MEM[R2 + 20] = R5
        Instruction(OpCode.NOP)
    ]

    REG[1] = 10
    REG[2] = 5
    MEM[20] = 99

# ================= MAIN =================

if __name__ == "__main__":
    load_test_program()
    run_pipeline(10)

    print("\nFinal Register State")
    for i in range(8):
        print(f"R{i} = {REG[i]}")