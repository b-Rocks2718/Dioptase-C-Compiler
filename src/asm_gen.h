#ifndef ASM_GEN_H
#define ASM_GEN_H

#include "AST.h"
#include "analysis.h"
#include "typechecking.h"
#include "TAC.h"
#include "slice.h"

#include <stdbool.h>
#include <stddef.h>

extern struct AsmSymbolTable* asm_symbol_table;

// Classify scalar and aggregate ASM operand types.
enum AsmTypeType {
  BYTE = 1,
  DOUBLE,
  WORD,
  LONG_WORD,
  BYTE_ARRAY,
};

// Describe the size and alignment of an aggregate byte array.
struct ByteArray {
  size_t size;
  size_t alignment;
};

// Store an ASM type kind and optional aggregate byte metadata.
struct AsmType {
  enum AsmTypeType type;
  struct ByteArray byte_array;
};

// Store an ASM symbol's type, linkage, definition state, and value.
struct AsmSymbolEntry{
  struct Slice* key;
  struct AsmType* type; // for data
  bool is_static;  // for data
  bool is_defined; // for functions
  bool return_on_stack; // for functions

  struct AsmSymbolEntry* next;
};

// Own the bucket array used for ASM symbol lookup.
struct AsmSymbolTable{
  size_t size;
  struct AsmSymbolEntry** arr;
};

// Own the linked list of top-level ASM declarations.
struct AsmProg {
  struct AsmTopLevel* head;
  struct AsmTopLevel* tail;
};

// Capture a stack-local debug entry for assembly emission.
struct DebugLocal {
  struct Slice* name;
  int offset;
  size_t size;
  struct DebugLocal* next;
};

// Classify ASM function and static-data top-level items.
enum AsmTopLevelType {
  ASM_FUNC,
  ASM_STATIC_VAR,
  ASM_STATIC_CONST,
  ASM_SECTION,
  ASM_ALIGN,
};

// Store an ASM function name, linkage, body, and debug locals.
struct AsmFunc {
  struct Slice* name;
  bool global;
  struct AsmInstr* body;
  struct DebugLocal* locals;
  size_t num_locals;
};

// Store a file-scope static variable and its initializer.
struct AsmStaticVar {
  struct Slice* name;
  bool global;
  int alignment;
  struct InitList* init_values;
};

// Store a file-scope static constant and its initializer.
struct AsmStaticConst {
  struct Slice* name;
  bool global;
  int alignment;
  struct InitList* init_values;
};

// Store an assembler section directive name.
struct AsmSection {
  struct Slice* name;
};

// Store an assembler alignment directive.
struct AsmAlign {
  int alignment;
};

// Select the concrete ASM top-level payload identified by AsmTopLevelType.
union AsmTopLevelVariant {
  struct AsmFunc asm_func;
  struct AsmStaticVar asm_static_var;
  struct AsmStaticConst asm_static_const;
  struct AsmSection asm_section;
  struct AsmAlign asm_align;
};

// Link one ASM top-level item with its kind and list pointer.
struct AsmTopLevel {
  enum AsmTopLevelType type;
  union AsmTopLevelVariant top;
  struct AsmTopLevel* next;
};

// Classify the ASM intermediate instruction variants.
enum AsmInstrType {
  ASM_MOV,
  ASM_UNARY,
  ASM_BINARY,
  ASM_CMP,
  ASM_PUSH,
  ASM_CALL,
  ASM_INDIRECT_CALL,
  ASM_JUMP,
  ASM_COND_JUMP,
  ASM_LABEL,
  ASM_RET,
  ASM_GET_ADDRESS,
  ASM_LOAD,
  ASM_STORE,
  ASM_BOUNDARY,
  ASM_TRUNC,
  ASM_EXTEND,
};

// Store source and destination operands for a move.
struct AsmMov {
  struct Operand* dst;
  struct Operand* src;
};

// Store unary operation, destination, and source operands.
struct AsmUnary {
  enum UnOp op;
  struct Operand* dst;
  struct Operand* src;
};

// Store ALU operation and its destination/source operands.
struct AsmBinary {
  enum ALUOp alu_op;
  struct Operand* dst;
  struct Operand* src1;
  struct Operand* src2;
};

// Store the two operands compared by a compare instruction.
struct AsmCmp {
  struct Operand* src1;
  struct Operand* src2;
};

// Store the value pushed onto the stack.
struct AsmPush {
  struct Operand* src;
};

// Store a direct call target label.
struct AsmCall {
  struct Slice* label;
};

// Store an indirect call target operand.
struct AsmIndirectCall {
  struct Operand* src;
};

// Store an unconditional jump target label.
struct AsmJump {
  struct Slice* label;
};

// Store a branch condition and target label.
struct AsmCondJump {
  enum TACCondition cond;
  struct Slice* label;
};

// Store the label defined by this instruction.
struct AsmLabel {
  struct Slice* label;
};

// Store destination and source for an address calculation.
struct AsmGetAddress {
  struct Operand* dst;
  struct Operand* src;
};

// Store destination value and source address for a load.
struct AsmLoad {
  struct Operand* dst;
  struct Operand* src;
};

// Store destination address and source value for a store.
struct AsmStore {
  struct Operand* dst;
  struct Operand* src;
};

// Store the source location associated with a debug boundary.
struct AsmBoundary {
  const char* loc;
};

// Describe narrowing conversion from src to target size.
struct AsmTrunc {
  struct Operand* dst;
  struct Operand* src;
  size_t size;
};

// Describe widening conversion from src size to destination type.
struct AsmExtend {
  struct Operand* dst;
  struct Operand* src;
  size_t size;
};

// Select the concrete ASM instruction payload identified by AsmInstrType.
union AsmInstrVariant {
  struct AsmMov asm_mov;
  struct AsmUnary asm_unary;
  struct AsmBinary asm_binary;
  struct AsmCmp asm_cmp;
  struct AsmPush asm_push;
  struct AsmCall asm_call;
  struct AsmIndirectCall asm_indirect_call;
  struct AsmJump asm_jump;
  struct AsmCondJump asm_cond_jump;
  struct AsmLabel asm_label;
  struct AsmGetAddress asm_get_address;
  struct AsmLoad asm_load;
  struct AsmStore asm_store;
  struct AsmBoundary asm_boundary;
  struct AsmTrunc asm_trunc;
  struct AsmExtend asm_extend;
};

// Link one ASM instruction with the next instruction in its list.
struct AsmInstr {
  enum AsmInstrType type;
  union AsmInstrVariant instr;
  struct AsmInstr* next;
};

// Classify register, immediate, memory, and pseudo operands.
enum OperandType {
  OPERAND_LIT,
  OPERAND_REG,
  OPERAND_PSEUDO,
  OPERAND_PSEUDO_MEM,
  OPERAND_MEMORY,
  OPERAND_DATA
};

// Enumerate target architectural registers used by the compiler.
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

// Immediate integer operand.
struct OperandLit {
  int value;
};

// Direct architectural register operand.
struct OperandReg {
  enum Reg reg;
};

// Compiler pseudo-register identified by name.
struct OperandPseudo {
  struct Slice* name;
};

// Named object accessed at a byte offset (aggregates and stack slots).
struct OperandPseudoMem {
  struct Slice* name;
  int offset;
};

// Address formed from a base register and a displacement.
struct OperandMemory {
  enum Reg base;
  int offset;
};

// Address formed from a data label and an optional displacement.
struct OperandData {
  struct Slice* label;
  int offset;
};

// Select the concrete operand payload identified by OperandType.
union OperandVariant {
  struct OperandLit lit;
  struct OperandReg reg;
  struct OperandPseudo pseudo;
  struct OperandPseudoMem pseudo_mem;
  struct OperandMemory memory;
  struct OperandData data;
};

// Store an ASM operand kind, assembly type, and the payload for that kind.
struct Operand {
  enum OperandType type;
  struct AsmType* asm_type;
  union OperandVariant op;
};

// Map one pseudo-register name to its allocated location.
struct PseudoEntry {
  struct Operand* pseudo;
  struct Operand* mapped;
  struct PseudoEntry* next;
};

// Own the bucket array used for pseudo-register mappings.
struct PseudoMap{
  size_t size;
  struct PseudoEntry** arr;
};

// Classify variables for the purpose of register allocation according to the ABI.
// MEMORY_CLASS indicates that the variable should be passed in memory.
// INTEGER_CLASS indicates that the variable can be passed in integer registers.
enum VarClass {
  MEMORY_CLASS,
  INTEGER_CLASS,
};

// Link variable storage-class annotations.
struct VarClassList {
  enum VarClass var_class;
  struct VarClassList* next;
};

// Link instruction operands in source order.
struct OperandList {
  struct Operand* opr;
  struct OperandList* next;
};

// Use caller-saved registers that are not argument registers for codegen scratch work.
extern const enum Reg kScratchRegA;
extern const enum Reg kScratchRegB;
extern const enum Reg kScratchRegC;

// Lower TAC into ASM, optionally emitting section directives.
// Returns the ASM program or exits on internal error.
struct AsmProg* prog_to_asm(struct TACProg* tac_prog, bool emit_sections);

struct AsmTopLevel* top_level_to_asm(struct TopLevel* tac_top);

struct AsmInstr* instr_to_asm(struct Slice* func_name, struct TACInstr* tac_instr);

struct AsmInstr* set_up_params(struct Slice* func_name,
                               struct Slice** params,
                               size_t num_params,
                               bool return_in_memory);

struct Operand* tac_val_to_asm(struct Val* val);

struct Operand* make_pseudo(struct Slice* var_name, struct AsmType* asm_type);

struct Operand* make_pseudo_mem(struct Slice* var_name, struct AsmType* asm_type, int offset);

struct Operand** get_ops(struct AsmInstr* asm_instr, size_t* out_count);

struct Operand** get_srcs(struct AsmInstr* asm_instr, size_t* out_count);

struct Operand* get_dst(struct AsmInstr* asm_instr);

// Build stack slot mappings for pseudo operands.
// Returns the total stack allocation in bytes (including reserved + padding).
// reserved_bytes preserves ABI-mandated slots (e.g., return pointer).
size_t create_maps(struct AsmInstr* asm_instr, size_t reserved_bytes);

void replace_pseudo(struct AsmInstr* asm_instr);

size_t type_alignment(struct Type* type, const struct Slice* symbol_name);

struct PseudoMap* create_pseudo_map(size_t numBuckets);

void pseudo_map_insert(struct PseudoMap* hmap, struct Operand* key, struct Operand* value);

struct Operand* pseudo_map_get(struct PseudoMap* hmap, struct Operand* key);

bool pseudo_map_contains(struct PseudoMap* hmap, struct Operand* key);

size_t asm_type_size(struct AsmType* type);

void print_pseudo_map(struct Slice* func, struct PseudoMap* hmap);

void destroy_pseudo_map(struct PseudoMap* hmap);

// Print a debugging representation of an ASM program.
// prog is the ASM program to print (may be NULL).
void print_asm_prog(const struct AsmProg* prog);

void print_asm_symbols(const struct AsmSymbolTable* sym_table);

// Detect whether a pseudo operand maps to a static storage symbol.
// Returns true if the operand names a static symbol.
bool is_static_symbol_operand(const struct Operand* opr);

// Reserve space for a new stack slot in the current frame.
// Returns the negative offset from BP for the new slot.
int allocate_stack_slot(struct Operand* opr, size_t* stack_bytes);

// Calculate total stack size needed for arguments passed on the stack.
// Returns the total size in bytes needed for stack arguments.
size_t get_stack_size(struct Val* args, size_t num_args);

// Replace a pseudo operand field with its mapped location if present.
void replace_operand_if_pseudo(struct Operand** field);

// classify function parameters into register and stack arguments
void classify_params(struct Val* params, size_t num_params, bool return_in_memory,
                     struct OperandList** reg_args, struct OperandList** stack_args);

// classify function return value into operand list of reg or pseudo mem
void classify_return_val(struct Val* ret_val, struct OperandList** ret_var_list, bool* return_in_memory);

size_t asm_type_alignment(struct AsmType* type);

struct AsmSymbolTable* create_asm_symbol_table(size_t numBuckets);

void asm_symbol_table_insert(struct AsmSymbolTable* hmap, struct Slice* key, 
    struct AsmType* type, bool is_static, bool is_defined, bool return_on_stack);

struct AsmSymbolEntry* asm_symbol_table_get(struct AsmSymbolTable* hmap, struct Slice* key);

bool asm_symbol_table_contains(struct AsmSymbolTable* hmap, struct Slice* key);

void print_asm_symbol_table(struct AsmSymbolTable* hmap);

ANALYSIS_NORETURN void asm_gen_error(const char* operation,
                          const struct Slice* func_name,
                          const char* fmt,
                          ...);

#endif // ASM_GEN_H
