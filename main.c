// FIANL PROJECT
// Group 7
// ECE 5367
// Jake Vandal, Nicholas Lloyd, Kenny Lam, Alejandro Castro, Hind Jaafar, Preston Hsu, Sebastian Padron
// Pipelined MiPS Simulator

/*
 * ============================================================
 *   Stages: IF  →  ID  →  EX  →  MEM  →  WB
 *
 *   Supported instructions:
 *     ADD  rd, rs, rt
 *     SUB  rd, rs, rt
 *     AND  rd, rs, rt
 *     OR   rd, rs, rt
 *     LW   rt, imm(rs)
 *     SW   rt, imm(rs)
 *     BEQ  rs, rt, offset
 *     NOP
 *
 *   Features:
 *     • Full forwarding (EX→EX, MEM→EX)
 *     • Load-use hazard stall (1 bubble inserted)
 *     • Branch resolved at MEM stage (3 bubbles on taken)
 *     • Cycle-by-cycle pipeline diagram printed
 * ============================================================
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ─── Constants ─────────────────────────────────────────── */
#define NUM_REGS      32
#define MEM_SIZE      256      /* words of data memory        */
#define MAX_INSTR     64       /* instruction memory capacity */
#define MAX_CYCLES    80       /* safety stop                 */

/* ─── Instruction opcodes (internal encoding) ────────────── */
#define OP_NOP  0
#define OP_ADD  1
#define OP_SUB  2
#define OP_AND  3
#define OP_OR   4
#define OP_LW   5
#define OP_SW   6
#define OP_BEQ  7

/* ─── Pipeline stage names (for display) ─────────────────── */
static const char *STAGE_NAME[] = {"  .", "IF", "ID", "EX", "ME", "WB"};
/* stage 0 → not yet / already done */

/* ─── Decoded instruction ─────────────────────────────────── */
typedef struct {
    int  op;          /* opcode                       */
    int  rs, rt, rd;  /* register indices             */
    int  imm;         /* sign-extended immediate      */
    int  pc;          /* PC of this instruction       */
    char label[32];   /* human-readable, for display  */
} Instr;

/* ─── Pipeline registers (latches) ───────────────────────── */
typedef struct { int  valid; Instr instr;                           } IF_ID_Latch;
typedef struct { int  valid; Instr instr; int32_t rv1, rv2;         } ID_EX_Latch;
typedef struct { int  valid; Instr instr; int32_t alu_out; int32_t rv2; } EX_MEM_Latch;
typedef struct { int  valid; Instr instr; int32_t result;            } MEM_WB_Latch;

/* ─── Processor state ─────────────────────────────────────── */
typedef struct {
    int32_t reg[NUM_REGS];
    int32_t dmem[MEM_SIZE];
    Instr   imem[MAX_INSTR];
    int     n_instr;

    int     pc;
    int     cycle;
    int     stall;        /* stall cycles remaining        */
    int     done;

    IF_ID_Latch  if_id;
    ID_EX_Latch  id_ex;
    EX_MEM_Latch ex_mem;
    MEM_WB_Latch mem_wb;

    /* tracking which stage each original instruction is in */
    int     stage_of[MAX_INSTR];   /* stage_of[i] = 0..5        */
} CPU;

/* ─── Helpers ─────────────────────────────────────────────── */
static Instr NOP_INSTR = { OP_NOP, 0,0,0,0,-1,"NOP" };

static void flush_latch_id_ex(CPU *cpu)  { cpu->id_ex  = (ID_EX_Latch){0};  cpu->id_ex.instr  = NOP_INSTR; }
static void flush_latch_if_id(CPU *cpu)  { cpu->if_id  = (IF_ID_Latch){0};  cpu->if_id.instr  = NOP_INSTR; }

/* ─── Build instruction ───────────────────────────────────── */
static Instr make_rtype(int op, int rd, int rs, int rt, const char *lbl) {
    Instr i = {0}; i.op=op; i.rd=rd; i.rs=rs; i.rt=rt;
    snprintf(i.label, sizeof(i.label), "%s", lbl); return i;
}
static Instr make_itype(int op, int rt, int rs, int imm, const char *lbl) {
    Instr i = {0}; i.op=op; i.rt=rt; i.rs=rs; i.imm=imm;
    snprintf(i.label, sizeof(i.label), "%s", lbl); return i;
}

/* ─── Load a program ─────────────────────────────────────── */
static void load_program(CPU *cpu, Instr *prog, int n) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->n_instr = n;
    for (int i = 0; i < n; i++) { prog[i].pc = i; cpu->imem[i] = prog[i]; }
    cpu->if_id.instr = NOP_INSTR;
    cpu->id_ex.instr = NOP_INSTR;
    cpu->ex_mem.instr = NOP_INSTR;
    cpu->mem_wb.instr = NOP_INSTR;
    /* seed registers for interesting output */
    cpu->reg[1]=10; cpu->reg[2]=20; cpu->reg[3]=30;
    cpu->reg[4]=5;  cpu->reg[5]=15;
    cpu->dmem[0]=100; cpu->dmem[1]=200; cpu->dmem[4]=50;
}

/* ─── Display helpers ────────────────────────────────────── */
static void print_separator(int n) {
    printf("+----------------------");
    for (int i=0;i<n;i++) printf("+------");
    printf("+\n");
}

static void print_pipeline_header(CPU *cpu) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║            PIPELINED MIPS SIMULATOR                 ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    printf("Initial Registers: R1=%d R2=%d R3=%d R4=%d R5=%d\n",
           cpu->reg[1],cpu->reg[2],cpu->reg[3],cpu->reg[4],cpu->reg[5]);
    printf("Initial Dmem[0]=%d Dmem[1]=%d Dmem[4]=%d\n\n",
           cpu->dmem[0],cpu->dmem[1],cpu->dmem[4]);
}

/* ─── Does an instruction write to a register? ───────────── */
static int writes_reg(int op, int *dst_out, const Instr *in) {
    switch (op) {
        case OP_ADD: case OP_SUB: case OP_AND: case OP_OR:
            *dst_out = in->rd;  return 1;
        case OP_LW:
            *dst_out = in->rt;  return 1;
        default:
            return 0;  /* SW, BEQ, NOP do NOT write */
    }
}

/* ─── Forwarding logic ───────────────────────────────────── */
static int32_t forward(CPU *cpu, int reg) {
    if (reg == 0) return 0;
    int dst;
    /* EX/MEM forward (highest priority) */
    if (cpu->ex_mem.valid && writes_reg(cpu->ex_mem.instr.op, &dst, &cpu->ex_mem.instr)) {
        if (dst == reg) return cpu->ex_mem.alu_out;
    }
    /* MEM/WB forward */
    if (cpu->mem_wb.valid && writes_reg(cpu->mem_wb.instr.op, &dst, &cpu->mem_wb.instr)) {
        if (dst == reg) return cpu->mem_wb.result;
    }
    return cpu->reg[reg];
}

/* ─── WB Stage ───────────────────────────────────────────── */
static void stage_WB(CPU *cpu) {
    if (!cpu->mem_wb.valid) return;
    Instr *in = &cpu->mem_wb.instr;
    int dst;
    if (writes_reg(in->op, &dst, in))
        cpu->reg[dst] = cpu->mem_wb.result;
    if (in->pc >= 0) cpu->stage_of[in->pc] = 0;
}

/* ─── MEM Stage ──────────────────────────────────────────── */
static void stage_MEM(CPU *cpu) {
    MEM_WB_Latch next = {0}; next.instr = NOP_INSTR;
    if (!cpu->ex_mem.valid) { cpu->mem_wb = next; return; }

    Instr *in  = &cpu->ex_mem.instr;
    int32_t ao = cpu->ex_mem.alu_out;

    next.valid = 1;
    next.instr = *in;

    if (in->op == OP_LW) {
        int addr = ao / 4;
        next.result = (addr>=0 && addr<MEM_SIZE) ? cpu->dmem[addr] : 0;
    } else if (in->op == OP_SW) {
        int addr = ao / 4;
        if (addr>=0 && addr<MEM_SIZE) cpu->dmem[addr] = cpu->ex_mem.rv2;
        next.result = 0;
    } else if (in->op == OP_BEQ) {
        /* branch already handled; just pass through */
        next.result = ao;
    } else {
        next.result = ao;
    }

    cpu->mem_wb = next;
    if (in->pc >= 0) cpu->stage_of[in->pc] = 5;
}

/* ─── EX Stage ───────────────────────────────────────────── */
static void stage_EX(CPU *cpu) {
    EX_MEM_Latch next = {0}; next.instr = NOP_INSTR;
    if (!cpu->id_ex.valid) { cpu->ex_mem = next; return; }

    Instr   *in  = &cpu->id_ex.instr;
    int32_t  rv1 = cpu->id_ex.rv1;
    int32_t  rv2 = cpu->id_ex.rv2;

    /* forwarding: override rv1/rv2 with latest values */
    if (in->op != OP_NOP) {
        rv1 = forward(cpu, in->rs);
        /* For R-type, SW, BEQ: rv2 is reg[rt]; for LW: rv2 unused */
        if (in->op != OP_LW)
            rv2 = forward(cpu, in->rt);
    }

    next.valid = 1;
    next.instr = *in;
    next.rv2   = rv2;   /* needed for SW */

    switch (in->op) {
        case OP_ADD: next.alu_out = rv1 + rv2;  break;
        case OP_SUB: next.alu_out = rv1 - rv2;  break;
        case OP_AND: next.alu_out = rv1 & rv2;  break;
        case OP_OR:  next.alu_out = rv1 | rv2;  break;
        case OP_LW:
        case OP_SW:  next.alu_out = rv1 + in->imm; break;
        case OP_BEQ: next.alu_out = (rv1 == rv2) ? 1 : 0; break;
        default:     next.alu_out = 0; break;
    }

    cpu->ex_mem = next;
    if (in->pc >= 0) cpu->stage_of[in->pc] = 4;
}

/* ─── ID Stage ───────────────────────────────────────────── */
static int stage_ID(CPU *cpu) {
    /* Returns 1 if a load-use stall is needed */
    if (!cpu->if_id.valid) {
        cpu->id_ex.valid = 0; cpu->id_ex.instr = NOP_INSTR; return 0;
    }

    Instr *in = &cpu->if_id.instr;

    /* Load-use hazard detection */
    if (cpu->id_ex.valid && cpu->id_ex.instr.op == OP_LW) {
        int load_dst = cpu->id_ex.instr.rt;
        if (load_dst != 0 &&
            (load_dst == in->rs || load_dst == in->rt)) {
            /* stall: do NOT advance IF/ID or ID/EX */
            cpu->id_ex.valid = 0;
            cpu->id_ex.instr = NOP_INSTR;
            return 1; /* signal stall */
        }
    }

    ID_EX_Latch next = {0};
    next.valid = 1;
    next.instr = *in;
    next.rv1   = cpu->reg[in->rs];
    /* rv2: R-type, BEQ, SW all need reg[rt]; LW doesn't use rv2 */
    switch (in->op) {
        case OP_ADD: case OP_SUB: case OP_AND: case OP_OR:
        case OP_BEQ: case OP_SW:
            next.rv2 = cpu->reg[in->rt]; break;
        default:
            next.rv2 = in->imm; break;  /* LW: unused, just store imm */
    }

    cpu->id_ex = next;
    if (in->pc >= 0) cpu->stage_of[in->pc] = 3;
    return 0;
}

/* ─── IF Stage ───────────────────────────────────────────── */
static void stage_IF(CPU *cpu) {
    IF_ID_Latch next = {0};
    if (cpu->pc < cpu->n_instr) {
        next.valid = 1;
        next.instr = cpu->imem[cpu->pc];
        cpu->stage_of[cpu->pc] = 2;
        cpu->pc++;
    } else {
        next.instr = NOP_INSTR;
    }
    cpu->if_id = next;
}

/* ─── Branch resolution (end of MEM) ────────────────────── */
static void resolve_branch(CPU *cpu) {
    if (!cpu->ex_mem.valid) return;
    if (cpu->ex_mem.instr.op != OP_BEQ) return;
    if (cpu->ex_mem.alu_out == 1) { /* branch taken */
        int target = cpu->ex_mem.instr.pc + 1 + cpu->ex_mem.instr.imm;
        cpu->pc = target;
        flush_latch_if_id(cpu);
        flush_latch_id_ex(cpu);
    }
}

/* ─── Check if pipeline is empty ─────────────────────────── */
static int pipeline_empty(CPU *cpu) {
    return !cpu->if_id.valid && !cpu->id_ex.valid &&
           !cpu->ex_mem.valid && !cpu->mem_wb.valid;
}

/* ─── Print one cycle row ─────────────────────────────────── */
static void print_cycle_row(CPU *cpu, int stalled) {
    printf("  Cycle %3d%s  │ ", cpu->cycle, stalled?" [STALL]":"         ");
    for (int i = 0; i < cpu->n_instr; i++) {
        int s = cpu->stage_of[i];
        printf(" %-2s  │", STAGE_NAME[s]);
    }
    printf("\n");
}

/* ─── Print pipeline diagram header ──────────────────────── */
static void print_diagram_header(CPU *cpu) {
    printf("\n  Pipeline Execution Diagram\n");
    printf("  %-18s│", "Cycle");
    for (int i = 0; i < cpu->n_instr; i++)
        printf(" %-4s │", cpu->imem[i].label);
    printf("\n");
    print_separator(cpu->n_instr);
}

/* ─── Run simulation ──────────────────────────────────────── */
static void run(CPU *cpu) {
    print_pipeline_header(cpu);
    print_diagram_header(cpu);

    int all_retired = 0;

    while (cpu->cycle < MAX_CYCLES && !all_retired) {
        cpu->cycle++;

        /* ── pipeline stages execute in reverse order ── */
        stage_WB(cpu);
        stage_MEM(cpu);
        resolve_branch(cpu);
        stage_EX(cpu);
        int stalled = stage_ID(cpu);

        if (stalled) {
            /* hold PC and IF/ID latch (repeat fetch next cycle) */
            if (cpu->pc > 0 && cpu->if_id.valid)
                cpu->pc--; /* re-fetch same instruction next cycle */
            /* IF/ID keeps its value; ID/EX gets a bubble (done in stage_ID) */
            print_cycle_row(cpu, 1);
        } else {
            /* Advance IF only if no stall */
            IF_ID_Latch old_if_id = cpu->if_id;
            stage_IF(cpu);
            (void)old_if_id;
            print_cycle_row(cpu, 0);
        }

        /* Check completion: all instructions retired and pipeline drained */
        all_retired = (cpu->pc >= cpu->n_instr) && pipeline_empty(cpu);
    }

    print_separator(cpu->n_instr);
    printf("  Total cycles: %d\n", cpu->cycle);

    /* Final register / memory state */
    printf("\n  ── Final Register State ──────────────────────────────\n");
    for (int i = 0; i < NUM_REGS; i++) {
        if (cpu->reg[i] != 0)
            printf("    R%-2d = %d\n", i, cpu->reg[i]);
    }
    printf("\n  ── Final Data Memory (non-zero words 0-15) ──────────\n");
    for (int i = 0; i < 16; i++) {
        if (cpu->dmem[i] != 0)
            printf("    Mem[%2d] = %d\n", i, cpu->dmem[i]);
    }
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════
 *  TEST CASES
 * ═══════════════════════════════════════════════════════════ */

/* ── Test 1: Simple sequential R-type (no hazards) ──────── */
static void test_sequential(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  TEST 1: Sequential R-type (No Hazards)             ║\n");
    printf("║  ADD R6,R1,R2   SUB R7,R3,R4   OR R8,R1,R5         ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /*   R1=10, R2=20, R3=30, R4=5, R5=15
         R6 = R1+R2 = 30
         R7 = R3-R4 = 25
         R8 = R1|R5 = 15
    */
    Instr prog[] = {
        make_rtype(OP_ADD, 6, 1, 2, "ADD"),
        make_rtype(OP_SUB, 7, 3, 4, "SUB"),
        make_rtype(OP_OR,  8, 1, 5, "OR "),
    };
    CPU cpu;
    load_program(&cpu, prog, 3);
    run(&cpu);
}

/* ── Test 2: Data hazards — forwarding handles them ─────── */
static void test_forwarding(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  TEST 2: Data Hazards — Forwarding                  ║\n");
    printf("║  ADD R6,R1,R2 → ADD R7,R6,R3 → SUB R8,R7,R4       ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /*   R6 = 10+20 = 30  (forwarded to next)
         R7 = 30+30 = 60  (forwarded to next)
         R8 = 60-5  = 55
    */
    Instr prog[] = {
        make_rtype(OP_ADD, 6, 1, 2, "ADD"),
        make_rtype(OP_ADD, 7, 6, 3, "ADD"),
        make_rtype(OP_SUB, 8, 7, 4, "SUB"),
    };
    CPU cpu;
    load_program(&cpu, prog, 3);
    run(&cpu);
}

/* ── Test 3: Load-use hazard → 1-cycle stall ─────────────── */
static void test_load_use(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  TEST 3: Load-Use Hazard (1 Stall Bubble)           ║\n");
    printf("║  LW R6,0(R0) → ADD R7,R6,R2                        ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /* dmem[0]=100, R2=20 → R7 = 100+20 = 120
       Pipeline must stall 1 cycle between LW and ADD */
    Instr prog[] = {
        make_itype(OP_LW,  6, 0,  0, "LW "),
        make_rtype(OP_ADD, 7, 6,  2, "ADD"),
        make_rtype(OP_OR,  8, 1,  5, "OR "),
    };
    CPU cpu;
    load_program(&cpu, prog, 3);
    run(&cpu);
}

/* ── Test 4: Store and Load ───────────────────────────────── */
static void test_store_load(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  TEST 4: SW then LW (Memory Operations)             ║\n");
    printf("║  SW R2,8(R0) → LW R9,8(R0) → ADD R10,R9,R4        ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /* Store R2(=20) to dmem[2], load it back into R9, R10=20+5=25 */
    Instr prog[] = {
        make_itype(OP_SW,  2, 0,  8, "SW "),
        make_itype(OP_LW,  9, 0,  8, "LW "),
        make_rtype(OP_ADD,10, 9,  4, "ADD"),
    };
    CPU cpu;
    load_program(&cpu, prog, 3);
    run(&cpu);
}

/* ── Test 5: Mixed — all features together ───────────────── */
static void test_mixed(void) {
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║  TEST 5: Mixed (R-type, LW, SW, Forwarding, Stall)  ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    /*  R1=10, R2=20, R3=30, R4=5
        ADD R6,R1,R2     → R6=30
        LW  R7,0(R0)     → R7=dmem[0]=100
        SW  R6,4(R0)     → dmem[1]=30
        ADD R8,R7,R6     → stall (LW→ADD), R8=130
        SUB R9,R8,R4     → R9=125
    */
    Instr prog[] = {
        make_rtype(OP_ADD,  6, 1, 2, "ADD"),
        make_itype(OP_LW,   7, 0, 0, "LW "),
        make_itype(OP_SW,   6, 0, 4, "SW "),
        make_rtype(OP_ADD,  8, 7, 6, "ADD"),
        make_rtype(OP_SUB,  9, 8, 4, "SUB"),
    };
    CPU cpu;
    load_program(&cpu, prog, 5);
    run(&cpu);
}

/* ─── Main ───────────────────────────────────────────────── */
int main(void) {
    test_sequential();
    test_forwarding();
    test_load_use();
    test_store_load();
    test_mixed();
    return 0;
}
