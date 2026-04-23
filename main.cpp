// FINAL PROJECT
// Group 7
// ECE 5367
// Jake Vandal, Nicholas Lloyd, Kenny Lam, Alejandro Castro, Hind Jaafar, Preston Hsu, Sebastian Padron
// Pipelined MIPS Simulator

#include <iostream>
#include <array>

using namespace std;

constexpr int NUM_REGS = 32;
constexpr int MEM_SIZE = 1024;
constexpr int MAX_INSTR = 100;

// ================= INSTRUCTION =================

enum class OpCode { ADD, SUB, LW, SW, NOP };

struct Instruction {
    OpCode op{};
    int rs{}, rt{}, rd{}, imm{};
};

// ================= PIPELINE REGISTERS =================

struct IF_ID { Instruction instr; };
struct ID_EX { Instruction instr; int rs_val{}, rt_val{}; };
struct EX_MEM { Instruction instr; int alu_result{}, rt_val{}; };
struct MEM_WB { Instruction instr; int mem_data{}, alu_result{}; };

// ================= GLOBAL STATE =================

array<int, NUM_REGS> REG{};
array<int, MEM_SIZE> MEM{};
array<Instruction, MAX_INSTR> instr_mem{};

IF_ID if_id;
ID_EX id_ex;
EX_MEM ex_mem;
MEM_WB mem_wb;

int pc = 0, num_instr = 0;
bool stall = false;

// ================= UTILITY =================

void print_instr(const Instruction& i) {
    switch (i.op) {
        case OpCode::ADD: cout << "ADD"; break;
        case OpCode::SUB: cout << "SUB"; break;
        case OpCode::LW:  cout << "LW";  break;
        case OpCode::SW:  cout << "SW";  break;
        case OpCode::NOP: cout << "NOP"; break;
    }
}

// ================= HAZARD DETECTION =================

bool detect_hazard(const Instruction& id,
                   const Instruction& ex,
                   const Instruction& mem) {
    auto check = [&](const Instruction& i) {
        int dest = (i.op == OpCode::LW) ? i.rt : i.rd;
        return dest && (id.rs == dest || id.rt == dest);
    };

    if (ex.op == OpCode::ADD || ex.op == OpCode::SUB || ex.op == OpCode::LW)
        if (check(ex)) return true;

    if (mem.op == OpCode::ADD || mem.op == OpCode::SUB || mem.op == OpCode::LW)
        if (check(mem)) return true;

    return false;
}

// ================= PIPELINE STAGES =================

void WB_stage() {
    if (mem_wb.instr.op == OpCode::ADD || mem_wb.instr.op == OpCode::SUB)
        REG[mem_wb.instr.rd] = mem_wb.alu_result;
    else if (mem_wb.instr.op == OpCode::LW)
        REG[mem_wb.instr.rt] = mem_wb.mem_data;
}

void MEM_stage() {
    mem_wb.instr = ex_mem.instr;
    mem_wb.alu_result = ex_mem.alu_result;

    if (ex_mem.instr.op == OpCode::LW)
        mem_wb.mem_data = MEM[ex_mem.alu_result];
    else if (ex_mem.instr.op == OpCode::SW)
        MEM[ex_mem.alu_result] = ex_mem.rt_val;
}

void EX_stage() {
    ex_mem.instr = id_ex.instr;

    switch (id_ex.instr.op) {
        case OpCode::ADD:
            ex_mem.alu_result = id_ex.rs_val + id_ex.rt_val;
            break;
        case OpCode::SUB:
            ex_mem.alu_result = id_ex.rs_val - id_ex.rt_val;
            break;
        case OpCode::LW:
        case OpCode::SW:
            ex_mem.alu_result = id_ex.rs_val + id_ex.instr.imm;
            ex_mem.rt_val = id_ex.rt_val;
            break;
        default: break;
    }
}

void ID_stage() {
    if (detect_hazard(if_id.instr, id_ex.instr, ex_mem.instr)) {
        stall = true;
        id_ex.instr.op = OpCode::NOP;
        return;
    }

    stall = false;
    id_ex.instr = if_id.instr;
    id_ex.rs_val = REG[if_id.instr.rs];
    id_ex.rt_val = REG[if_id.instr.rt];
}

void IF_stage() {
    if (stall) return;

    if (pc < num_instr)
        if_id.instr = instr_mem[pc++];
    else
        if_id.instr.op = OpCode::NOP;
}

// ================= SIMULATION =================

void run_pipeline(int cycles) {
    for (int i = 0; i < cycles; i++) {
        cout << "\nCycle " << i + 1 << "\n";

        WB_stage(); MEM_stage(); EX_stage(); ID_stage(); IF_stage();

        cout << "IF:  "; print_instr(if_id.instr); cout << "\n";
        cout << "ID:  "; print_instr(id_ex.instr); cout << "\n";
        cout << "EX:  "; print_instr(ex_mem.instr); cout << "\n";
        cout << "MEM: "; print_instr(mem_wb.instr); cout << "\n";
    }
}

// ================= MAIN =================

int main() {
    instr_mem[0] = {OpCode::ADD, 1, 2, 3, 0};
    instr_mem[1] = {OpCode::SUB, 3, 1, 4, 0};
    instr_mem[2] = {OpCode::LW,  1, 5, 0, 10};
    instr_mem[3] = {OpCode::SW,  2, 5, 0, 20};
    instr_mem[4] = {OpCode::NOP, 0, 0, 0, 0};

    REG[1] = 10;
    REG[2] = 5;
    MEM[20] = 99;
    num_instr = 5;

    run_pipeline(10);

    cout << "\nFinal Register State\n";
    for (int i = 0; i < 8; i++)
        cout << "R" << i << " = " << REG[i] << "\n";
}