// FINAL PROJECT
// Group 7
// ECE 5367
// Jake Vandal, Nicholas Lloyd, Kenny Lam, Alejandro Castro, Hind Jaafar, Preston Hsu, Sebastian Padron
// Pipelined MiPS Simulator

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_REGS 32
#define MEM_SIZE 1024
#define MAX_INSTR 100

// Instruction types
typedef enum {
    ADD, SUB, LW, SW, NOP
} OpCode;

// Instruction structure
typedef struct {
    OpCode op;
    int rs, rt, rd;
    int imm;
} Instruction;

// Pipeline registers
typedef struct {
    Instruction instr;
    int pc;
} IF_ID;

typedef struct {
    Instruction instr;
    int rs_val, rt_val;
} ID_EX;

typedef struct {
    Instruction instr;
    int alu_result;
    int rt_val;
} EX_MEM;

typedef struct {
    Instruction instr;
    int mem_data;
    int alu_result;
} MEM_WB;

// Global state
int REG[NUM_REGS] = {0};
int MEM[MEM_SIZE] = {0};
Instruction instr_mem[MAX_INSTR];

IF_ID if_id;
ID_EX id_ex;
EX_MEM ex_mem;
MEM_WB mem_wb;

int pc = 0;
int num_instr = 0;

int stall = 0; // Add this!

// Add prototypes or move definitions
int detect_hazard(Instruction id_instr, Instruction ex_instr, Instruction mem_instr);

// Utility: print instruction
void print_instr(Instruction instr) {
    switch(instr.op) {
        case ADD: printf("ADD"); break;
        case SUB: printf("SUB"); break;
        case LW:  printf("LW"); break;
        case SW:  printf("SW"); break;
        case NOP: printf("NOP"); break;
    }
}

// ================= PIPELINE STAGES =================

void WB_stage() {
    Instruction instr = mem_wb.instr;

    if (instr.op == ADD || instr.op == SUB) {
        REG[instr.rd] = mem_wb.alu_result;
    } else if (instr.op == LW) {
        REG[instr.rt] = mem_wb.mem_data;
    }
}

void MEM_stage() {
    Instruction instr = ex_mem.instr;
    mem_wb.instr = instr;

    if (instr.op == LW) {
        mem_wb.mem_data = MEM[ex_mem.alu_result];
    } else if (instr.op == SW) {
        MEM[ex_mem.alu_result] = ex_mem.rt_val;
    }

    mem_wb.alu_result = ex_mem.alu_result;
}

void EX_stage() {
    Instruction instr = id_ex.instr;
    ex_mem.instr = instr;

    switch(instr.op) {
        case ADD:
            ex_mem.alu_result = id_ex.rs_val + id_ex.rt_val;
            break;
        case SUB:
            ex_mem.alu_result = id_ex.rs_val - id_ex.rt_val;
            break;
        case LW:
        case SW:
            ex_mem.alu_result = id_ex.rs_val + instr.imm;
            ex_mem.rt_val = id_ex.rt_val;
            break;
        default:
            break;
    }
}

void ID_stage() {
    Instruction instr = if_id.instr;

    // Pass the instruction in EX and the instruction in MEM to check for hazards
    if (detect_hazard(instr, id_ex.instr, ex_mem.instr)) {
        stall = 1;
        // Injecting a NOP into the EX stage is correct
        id_ex.instr.op = NOP; 
        return;
    }

    stall = 0;
    id_ex.instr = instr;
    id_ex.rs_val = REG[instr.rs];
    id_ex.rt_val = REG[instr.rt];
}

void IF_stage() {
    if (stall) {
        // Do NOT fetch new instruction
        return;
    }

    if (pc < num_instr) {
        if_id.instr = instr_mem[pc];
        if_id.pc = pc;
        pc++;
    } else {
        if_id.instr.op = NOP;
    }
}
int detect_hazard(Instruction id_instr, Instruction ex_instr, Instruction mem_instr) {
    // Check against EX stage
    if (ex_instr.op == ADD || ex_instr.op == SUB || ex_instr.op == LW) {
        int dest = (ex_instr.op == LW) ? ex_instr.rt : ex_instr.rd;
        if (dest != 0 && (id_instr.rs == dest || id_instr.rt == dest)) return 1;
    }
    
    // Check against MEM stage (This was missing!)
    if (mem_instr.op == ADD || mem_instr.op == SUB || mem_instr.op == LW) {
        int dest = (mem_instr.op == LW) ? mem_instr.rt : mem_instr.rd;
        if (dest != 0 && (id_instr.rs == dest || id_instr.rt == dest)) return 1;
    }

    return 0;
}

// ================= SIMULATION =================

void run_pipeline(int cycles) {
    int i; // Declare it here
    for (i = 0; i < cycles; i++) {
        printf("\nCycle %d:\n", i + 1);

        WB_stage();
        MEM_stage();
        EX_stage();
        ID_stage();
        IF_stage();

        printf("IF: "); print_instr(if_id.instr); printf("\n");
        printf("ID: "); print_instr(id_ex.instr); printf("\n");
        printf("EX: "); print_instr(ex_mem.instr); printf("\n");
        printf("MEM: "); print_instr(mem_wb.instr); printf("\n");
    }
}

// ================= TEST PROGRAM =================

void load_test_program() {
    num_instr = 5;

    instr_mem[0] = (Instruction){ADD, 1, 2, 3, 0}; // R3 = R1 + R2
    instr_mem[1] = (Instruction){SUB, 3, 1, 4, 0}; // R4 = R3 - R1
    instr_mem[2] = (Instruction){LW,  1, 5, 0, 10}; // R5 = MEM[R1+10]
    instr_mem[3] = (Instruction){SW,  2, 5, 0, 20}; // MEM[R2+20] = R5
    instr_mem[4] = (Instruction){NOP, 0, 0, 0, 0};

    REG[1] = 10;
    REG[2] = 5;
    MEM[20] = 99;
}

// ================= MAIN =================

int main() {
    int i; // Declare it here
    if_id.instr.op = NOP;
    id_ex.instr.op = NOP;
    ex_mem.instr.op = NOP;
    mem_wb.instr.op = NOP;
    load_test_program();
    run_pipeline(10);

    printf("\nFinal Register State:\n");
    for (i = 0; i < 8; i++) {
        printf("R%d = %d\n", i, REG[i]);
    }

    return 0;
}
