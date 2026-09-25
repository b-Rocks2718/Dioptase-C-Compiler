#ifndef TAC_H
#define TAC_H

#include "AST.h"
#include "typechecking.h"
#include "copy_prop.h"

#include <stdint.h>

// Own the TAC top-level list and file-scope static values.
struct TACProg {
  struct TopLevel* head;    // Function top-levels in source order.
  struct TopLevel* tail;    // Tail of the function list for append operations.
  struct TopLevel* statics; // Static variable entries collected from symbols.
};

// Classify function, global, and static TAC top-level items.
enum TopLevelType {
  FUNC,
  STATIC_VAR,
  STATIC_CONST,
};

// Own a TAC instruction list with O(1) append via an explicit tail pointer.
// Empty lists have head == last == NULL. Non-empty lists have last as the
// final node of the head-linked chain (last->next == NULL).
struct TACInstrList {
  struct TACInstr* head;
  struct TACInstr* last;
};

// Store function name, linkage, parameters, and TAC body.
struct TACFunc {
  struct Slice* name;
  bool global;
  struct TACInstrList body;
  struct Slice** params;
  size_t num_params;
};

// Store a file-scope static variable and its initializer.
struct TACStaticVar {
  struct Slice* name;
  bool global;
  struct Type* var_type;
  struct InitList* init_values;
};

// Store a file-scope static constant and its initializer.
struct TACStaticConst {
  struct Slice* name;
  bool global;
  struct Type* var_type;
  struct InitList* init_values;
};

// Select the concrete TAC top-level payload identified by TopLevelType.
union TopLevelVariant {
  struct TACFunc tac_func;
  struct TACStaticVar tac_static_var;
  struct TACStaticConst tac_static_const;
};

// Link one TAC top-level item with its kind and list pointer.
struct TopLevel {
  enum TopLevelType type;
  union TopLevelVariant top;
  struct TopLevel* next;
};

// Distinguish constant and variable TAC operands.
enum ValType {
  CONSTANT,
  VARIABLE
};

// Select constant or variable-name payload for a TAC value.
union ValVariant {
  uint64_t const_value; // stores raw constant bits for 32/64-bit integers
  struct Slice* var_name;
};

// Store TAC value kind, payload, and static type.
struct Val {
  enum ValType val_type;
  union ValVariant val;
  struct Type* type;
};

// Classify the TAC instruction variants.
enum TACInstrType {
  TACRETURN,
  TACUNARY,
  TACBINARY,
  TACCOND_JUMP,
  TACJUMP,
  TACLABEL,
  TACCOPY,
  // Volatile accesses are side effects. They reuse the copy, load, store, and
  // offset-copy payloads, and optimization passes must not delete or forward them.
  TACVOLATILE_READ,
  TACVOLATILE_WRITE,
  TACVOLATILE_LOAD,
  TACVOLATILE_STORE,
  TACVOLATILE_COPY_TO_OFFSET,
  TACVOLATILE_COPY_FROM_OFFSET,
  TACCALL,
  TACCALL_INDIRECT,
  TACTAIL_CALL,
  TACTAIL_CALL_INDIRECT,
  TACGET_ADDRESS,
  TACLOAD,
  TACSTORE,
  TACCOPY_TO_OFFSET,
  TACCOPY_FROM_OFFSET,
  TACBOUNDARY,
  TACTRUNC,
  TACEXTEND,
};

// Enumerate conditions used by TAC conditional jumps.
enum TACCondition {
  CondE,
  CondNE,
  CondG,
  CondGE,
  CondL,
  CondLE,
  CondA,
  CondAE,
  CondB,
  CondBE
};

// Store the optional return operand.
struct TACReturn {
  struct Val* src;
};

// Store unary operation, destination, and source operands.
struct TACUnary {
  enum UnOp op;
  struct Val* dst;
  struct Val* src;
};

// Enumerate arithmetic and bitwise TAC ALU operations.
enum ALUOp {
  ALU_ADD,
  ALU_SUB,
  ALU_SMUL,
  ALU_SDIV,
  ALU_SMOD,
  ALU_UMUL,
  ALU_UDIV,
  ALU_UMOD,
  ALU_AND,
  ALU_OR,
  ALU_XOR,
  ALU_LSL,
  ALU_LSR,
  ALU_ASL,
  ALU_ASR,
  ALU_MOV, // ignore first arg, copy second arg to dst
};

// Store ALU operation and its destination/source operands.
struct TACBinary {
  enum ALUOp alu_op;
  struct Val* dst;
  struct Val* src1;
  struct Val* src2;
};

// Store a branch condition and target label.
struct TACCondJump {
  struct Val* src1;
  struct Val* src2;
  enum TACCondition condition;
  struct Slice* label;
};

// Store the unconditional jump target label.
struct TACJump {
  struct Slice* label;
};

// Store the label defined by this TAC instruction.
struct TACLabel {
  struct Slice* label;
};

// Store source and destination operands for a copy.
struct TACCopy {
  struct Val* dst;
  struct Val* src;
};

// Store direct call target, result destination, and arguments.
struct TACCall {
  struct Slice* func_name;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// Store indirect call target, result destination, and arguments.
struct TACCallIndirect {
  struct Val* func;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// Store tail call target, result destination, and arguments.
struct TACTailCall {
  struct Slice* func_name;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// Store indirect tail call target, result destination, and arguments.
struct TACTailCallIndirect {
  struct Val* func;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// Store source object and destination pointer for address calculation.
struct TACGetAddress {
  struct Val* dst;
  struct Val* src;
};

// Store destination value and source address for a load.
struct TACLoad {
  struct Val* dst;
  struct Val* src_ptr;
};

// Store destination address and source value for a store.
struct TACStore {
  struct Val* dst_ptr;
  struct Val* src;
};

// Describe an aggregate copy into a destination byte offset.
struct TACCopyToOffset {
  struct Slice* dst;
  struct Val* src;
  int offset;
  struct Type* dst_type;
};

// Describe an aggregate copy from a source byte offset.
struct TACCopyFromOffset {
  struct Val* dst;
  struct Slice* src;
  int offset;
};

// Store the source location associated with a debug boundary.
struct TACBoundary {
  const char* loc; // start of the statement for debug line markers
};

// Describe narrowing conversion from src to target_size.
struct TACTrunc {
  struct Val* dst;
  struct Val* src;
  size_t target_size; // in bytes
};

// Describe widening conversion from src_size to destination type.
struct TACExtend {
  struct Val* dst;
  struct Val* src;
  size_t src_size; // in bytes
};

// Select the concrete TAC instruction payload identified by TacInstrType.
union TACInstrVariant {
  struct TACReturn tac_return;
  struct TACUnary tac_unary;
  struct TACBinary tac_binary;
  struct TACCondJump tac_cond_jump;
  struct TACJump tac_jump;
  struct TACLabel tac_label;
  struct TACCopy tac_copy;
  struct TACCall tac_call;
  struct TACCallIndirect tac_call_indirect;
  struct TACTailCall tac_tail_call;
  struct TACTailCallIndirect tac_tail_call_indirect;
  struct TACGetAddress tac_get_address;
  struct TACLoad tac_load;
  struct TACStore tac_store;
  struct TACCopyToOffset tac_copy_to_offset;
  struct TACCopyFromOffset tac_copy_from_offset;
  struct TACBoundary tac_boundary; // used for statement/declaration debug line markers
  struct TACTrunc tac_trunc;
  struct TACExtend tac_extend;
};

// Link one TAC instruction with the next instruction in its list.
// Optimizer analyses keep their per-instruction facts in pass-local arrays,
// so an instruction carries no analysis annotations.
struct TACInstr {
  enum TACInstrType type;
  union TACInstrVariant instr;
  struct TACInstr* next;
};

// Classify whether an expression result is a value or aggregate location.
enum ExprResultType {
  PLAIN_OPERAND,
  DEREFERENCED_POINTER,
  SUB_OBJECT,
};

// Store lowered expression value and optional aggregate subobject metadata.
struct ExprResult {
  enum ExprResultType type;
  struct Val* val;
  struct Slice* sub_object_base; // for SUB_OBJECT
  int sub_object_offset;          // for SUB_OBJECT
};

// ----- Main TAC conversion functions -----

// Lower a full program into TAC, optionally emitting debug boundaries and tail calls.
// Returns a TAC program with top-level lists or NULL on failure.
struct TACProg* prog_to_TAC(struct Program* program, bool emit_debug_info, bool enable_tail_calls);

struct TopLevel* file_scope_dclr_to_TAC(struct Declaration* declaration);

struct TopLevel* symbol_to_TAC(struct SymbolEntry* symbol);

struct TopLevel* func_to_TAC(struct FunctionDclr* declaration);

struct TACInstrList block_to_TAC(struct Slice* func_name, struct Block* block);

struct TACInstrList local_dclr_to_TAC(struct Slice* func_name, struct Declaration* dclr);

struct TACInstrList var_dclr_to_TAC(struct Slice* func_name, struct Declaration* dclr);

struct TACInstrList stmt_to_TAC(struct Slice* func_name, struct Statement* stmt);

struct TACInstrList expr_to_TAC_convert(struct Slice* func_name, struct Expr* expr, struct Val* out_val);

struct TACInstrList expr_to_TAC(struct Slice* func_name, struct Expr* expr, struct ExprResult* result);

struct TACInstrList if_to_TAC(struct Slice* func_name, struct Expr* condition, struct Statement* if_stmt);

struct TACInstrList if_else_to_TAC(struct Slice* func_name, struct Expr* condition, struct Statement* if_stmt, struct Statement* else_stmt);

struct TACInstrList cases_to_TAC(struct Slice* func_name, struct Slice* label, struct CaseList* cases, struct Val* rslt);

struct TACInstrList relational_to_TAC(struct Slice* func_name,
                                          struct Expr* expr,
                                          enum BinOp op,
                                          struct Expr* left,
                                          struct Expr* right,
                                          struct ExprResult* result);

struct TACInstrList args_to_TAC(struct Slice* func_name,
                                    struct ArgList* args,
                                    struct Val** out_args,
                                    size_t* out_count);

struct TACInstrList for_init_to_TAC(struct Slice* func_name, struct ForInit* init_);

struct TACInstrList while_to_TAC(struct Slice* func_name,
                                     struct Expr* condition,
                                     struct Statement* body,
                                     struct Slice* label);

struct TACInstrList do_while_to_TAC(struct Slice* func_name,
                                        struct Statement* body,
                                        struct Expr* condition,
                                        struct Slice* label);

struct TACInstrList for_to_TAC(struct Slice* func_name,
                                   struct ForInit* init_,
                                   struct Expr* condition,
                                   struct Expr* end,
                                   struct Statement* body,
                                   struct Slice* label,
                                   struct IdentMap* idents);

// Lower a truth value into a branch; invert selects the false case.
struct TACInstrList cond_to_TAC(struct Slice* func_name, struct Expr* condition,
    struct Slice* target, bool invert);

// Lower a function call into TAC. If is_tail_call is true, generate a tail call.
struct TACInstrList call_to_TAC(struct Slice* func_name, struct Expr* expr, struct ExprResult* result, bool is_tail_call);

// ----- Utility functions -----

// Build a one-element list, or an empty list if instr is NULL.
struct TACInstrList tac_instr_list(struct TACInstr* instr);

// Append src onto dst. Empty src is a no-op; empty dst becomes src.
void concat_TAC_instrs(struct TACInstrList* dst, struct TACInstrList src);

struct Val* make_temp(struct Slice* func_name, struct Type* type);

// Return true if name refers to a variable with static storage duration.
// Temps, function symbols, static constants, and missing symbol-table
// entries are not static variables.
bool is_static_var(struct Slice* name);

// returns true for signed long, int, char
// returns false for unsigned long, int, char, and pointer types
bool tac_signedness(struct Type* type);

void print_static_init(const struct InitList* init);

bool compare_bodies(struct TACInstr* body1, struct TACInstr* body2);

// Copy src into a new list in reverse order.
// Operand pointers are shared with src. Analysis annotations start empty.
struct TACInstrList reverse_instr_list(struct TACInstrList src);

// Print one TAC instruction to stdout with the requested indentation.
void print_tac_instr(const struct TACInstr* instr, unsigned tabs);

// Print a linked list of TAC instructions.
void print_tac_instrs(const struct TACInstr* instrs, unsigned tabs);

void print_tac_prog(struct TACProg* prog);

// ----- TAC interpreter -----

// Execute a TAC program and return the integer result of main().
int tac_interpret_prog(const struct TACProg* prog);

#ifdef TAC_INTERNAL
static void tac_error_at(const char* loc, const char* fmt, ...);

static struct TACInstr* tac_instr_create(enum TACInstrType type);

static struct Val* tac_make_const(uint64_t value, struct Type* type);

static struct Val* tac_make_var(struct Slice* name, struct Type* type);

static void tac_copy_val(struct Val* dst, const struct Val* src);

static struct Slice* tac_make_label(struct Slice* func_name, const char* suffix);

static bool is_relational_op(enum BinOp op);

static bool is_compound_op(enum BinOp op);

static enum BinOp compound_to_binop(enum BinOp op);

static enum TACCondition relation_to_cond(enum BinOp op, struct Type* type);
#endif

#endif // TAC_H
