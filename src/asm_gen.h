#ifndef ASM_GEN_H
#define ASM_GEN_H

#include "AST.h"
#include "typechecking.h"
#include "TAC.h"
#include "slice.h"

#include <stdbool.h>
#include <stddef.h>

enum AsmType {
  BYTE,
  DOUBLE,
  WORD,
  LONG_WORD
};

struct AsmProg {
  struct AsmTopLevel* head;
  struct AsmTopLevel* tail;
};

enum AsmTopLevelType {
    ASM_FUNC,
    ASM_STATIC_VAR,
    ASM_SECTION,
};

struct AsmTopLevel {
    enum AsmTopLevelType type;
    struct Slice* name;
    bool global;

    struct AsmInstr* body; // for Func

    int alignment; // for StaticVar
    struct IdentInit* init_values; // for StaticVar
    size_t num_inits; // for StaticVar

    struct AsmTopLevel* next;
};

enum AsmInstrType {
    ASM_MOV,
    ASM_MOVSX,
    ASM_UNARY,
    ASM_BINARY,
    ASM_CMP,
    ASM_PUSH,
    ASM_CALL,
    ASM_JUMP,
    ASM_COND_JUMP,
    ASM_LABEL,
    ASM_RET,
    ASM_GET_ADDRESS,
};

struct AsmInstr {
    enum AsmInstrType type;

    enum UnOp unary_op; // for Unary
    enum ALUOp alu_op;     // for Binary
    enum TACCondition cond;   // for CondJump

    struct Operand* dst;
    struct Operand* src1;
    struct Operand* src2; // for binary

    struct Slice* label; // for Jump, CondJump, Label

    struct AsmInstr* next;
};

enum OperandType {
    OPERAND_LIT,
    OPERAND_REG,
    OPERAND_PSEUDO,
    OPERAND_PSEUDO_MEM,
    OPERAND_MEMORY,
    OPERAND_DATA
};

enum Reg {
    R0 = 0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R16,
    R17,
    R18,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    R26,
    R27,
    R28,
    R29,
    R30,
    R31
};

static const enum Reg BP = R30; // base pointer register
static const enum Reg SP = R31; // stack pointer register
static const enum Reg RA = R29; // return address register

struct Operand {
    enum OperandType type;
    
    enum Reg reg;          // for Reg / Memory
    int lit_value;        // for Lit / PsuedoMem / Memory
    struct Slice* pseudo;  // for Pseudo
};

struct PseudoEntry {
    struct Operand* pseudo;
    struct Operand* mapped;
    struct PseudoEntry* next;
};

struct PseudoMap{
    size_t size;
    struct PseudoEntry** arr;
};

struct AsmProg* prog_to_asm(struct TACProg* tac_prog);

struct AsmTopLevel* top_level_to_asm(struct TopLevel* tac_top);

struct AsmInstr* instr_to_asm(struct Slice* func_name, struct TACInstr* tac_instr);

struct AsmInstr* params_to_asm(struct Slice** params, size_t num_params);

struct Operand* tac_val_to_asm(struct Val* val);

struct Operand* make_pseudo_mem(struct Val* val, int offset);

struct Operand** get_ops(struct AsmInstr* asm_instr, size_t* out_count);

struct Operand** get_srcs(struct AsmInstr* asm_instr, size_t* out_count);

struct Operand* get_dst(struct AsmInstr* asm_instr);

size_t create_maps(struct AsmInstr* asm_instr);

void replace_pseudo(struct AsmInstr* asm_instr);

size_t type_alignment(struct Type* type, const struct Slice* symbol_name);

struct PseudoMap* create_pseudo_map(size_t numBuckets);

void pseudo_map_insert(struct PseudoMap* hmap, struct Operand* key, struct Operand* value);

struct Operand* pseudo_map_get(struct PseudoMap* hmap, struct Operand* key);

bool pseudo_map_contains(struct PseudoMap* hmap, struct Operand* key);

void destroy_pseudo_map(struct PseudoMap* hmap);

// Purpose: Print a debugging representation of an ASM program.
// Inputs: prog is the ASM program to print (may be NULL).
// Outputs: Writes a readable summary to stdout.
// Invariants/Assumptions: The program list is well-formed and acyclic.
void print_asm_prog(const struct AsmProg* prog);

#endif // ASM_GEN_H
