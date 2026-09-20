#ifndef TAC_H
#define TAC_H

#include "AST.h"
#include "typechecking.h"

#include <stdint.h>

// The tacprog stores head, tail, statics.
struct TACProg {
  struct TopLevel* head;    // Function top-levels in source order.
  struct TopLevel* tail;    // Tail of the function list for append operations.
  struct TopLevel* statics; // Static variable entries collected from symbols.
};

// Identify the possible top level type values.
enum TopLevelType {
  FUNC,
  STATIC_VAR,
  STATIC_CONST,
};

// The top level stores type, name, global, body, and other fields.
struct TopLevel {
  enum TopLevelType type;
  struct Slice* name;
  bool global;

  struct TACInstr* body; // for Func
  struct Slice** params; // for Func
  size_t num_params;    // for Func
  
  struct Type* var_type; // for StaticVar and StaticConst
  struct InitList* init_values; // for StaticVar and StaticConst
  
  struct TopLevel* next;
};

// Identify the possible val type values.
enum ValType {
  CONSTANT,
  VARIABLE
};

// The val variant stores const_value, var_name.
union ValVariant {
  uint64_t const_value; // stores raw constant bits for 32/64-bit integers
  struct Slice* var_name;
};

// The val stores val_type, val, type.
struct Val {
  enum ValType val_type;
  union ValVariant val;
  struct Type* type;
};

// Identify the possible tacinstr type values.
enum TACInstrType {
  TACRETURN,
  TACUNARY,
  TACBINARY,
  TACCOND_JUMP,
  TACJUMP,
  TACLABEL,
  TACCOPY,
  TACCALL,
  TACCALL_INDIRECT,
  TACGET_ADDRESS,
  TACLOAD,
  TACSTORE,
  TACCOPY_TO_OFFSET,
  TACCOPY_FROM_OFFSET,
  TACBOUNDARY,
  TACTRUNC,
  TACEXTEND,
};

// Identify the possible taccondition values.
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

// The tacreturn stores dst.
struct TACReturn {
  struct Val* dst;
};

// The tacunary stores op, dst, src.
struct TACUnary {
  enum UnOp op;
  struct Val* dst;
  struct Val* src;
};

// Identify the possible aluop values.
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

// The tacbinary stores alu_op, dst, src1, src2.
struct TACBinary {
  enum ALUOp alu_op;
  struct Val* dst;
  struct Val* src1;
  struct Val* src2;
};

// The taccond jump stores src1, src2, condition, label.
struct TACCondJump {
  struct Val* src1;
  struct Val* src2;
  enum TACCondition condition;
  struct Slice* label;
};

// The tacjump stores label.
struct TACJump {
  struct Slice* label;
};

// The taclabel stores label.
struct TACLabel {
  struct Slice* label;
};

// The taccopy stores dst, src.
struct TACCopy {
  struct Val* dst;
  struct Val* src;
};

// The taccall stores func_name, dst, args, num_args.
struct TACCall {
  struct Slice* func_name;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// The taccall indirect stores func, dst, args, num_args.
struct TACCallIndirect {
  struct Val* func;
  struct Val* dst;
  struct Val* args;
  size_t num_args;
};

// The tacget address stores dst, src.
struct TACGetAddress {
  struct Val* dst;
  struct Val* src;
};

// The tacload stores dst, src_ptr.
struct TACLoad {
  struct Val* dst;
  struct Val* src_ptr;
};

// The tacstore stores dst_ptr, src.
struct TACStore {
  struct Val* dst_ptr;
  struct Val* src;
};

// The taccopy to offset stores dst, src, offset, dst_type.
struct TACCopyToOffset {
  struct Slice* dst;
  struct Val* src;
  int offset;
  struct Type* dst_type;
};

// The taccopy from offset stores dst, src, offset.
struct TACCopyFromOffset {
  struct Val* dst;
  struct Slice* src;
  int offset;
};

// The tacboundary stores loc.
struct TACBoundary {
  const char* loc; // start of the statement for debug line markers
};

// The tactrunc stores dst, src, target_size.
struct TACTrunc {
  struct Val* dst;
  struct Val* src;
  size_t target_size; // in bytes
};

// The tacextend stores dst, src, src_size.
struct TACExtend {
  struct Val* dst;
  struct Val* src;
  size_t src_size; // in bytes
};

// The tacinstr variant stores tac_return, tac_unary, tac_binary, tac_cond_jump, and other fields.
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
  struct TACGetAddress tac_get_address;
  struct TACLoad tac_load;
  struct TACStore tac_store;
  struct TACCopyToOffset tac_copy_to_offset;
  struct TACCopyFromOffset tac_copy_from_offset;
  struct TACBoundary tac_boundary; // used for statement/declaration debug line markers
  struct TACTrunc tac_trunc;
  struct TACExtend tac_extend;
};

// The tacinstr stores type, instr, next, last.
struct TACInstr {
  enum TACInstrType type;
  union TACInstrVariant instr;
  struct TACInstr* next;
  struct TACInstr* last; // for convenience in building lists
};

// Identify the possible expr result type values.
enum ExprResultType {
  PLAIN_OPERAND,
  DEREFERENCED_POINTER,
  SUB_OBJECT,
};

// The a expr result stores type, val, sub_object_base, sub_object_offset.
struct ExprResult {
  enum ExprResultType type;
  struct Val* val;
  struct Slice* sub_object_base; // for SUB_OBJECT
  int sub_object_offset;          // for SUB_OBJECT
};

// ----- Main TAC conversion functions -----

// Lower a full program into TAC, optionally emitting debug boundaries.
// Returns a TAC program with top-level lists or NULL on failure.
struct TACProg* prog_to_TAC(struct Program* program, bool emit_debug_info);

struct TopLevel* file_scope_dclr_to_TAC(struct Declaration* declaration);

struct TopLevel* symbol_to_TAC(struct SymbolEntry* symbol);

struct TopLevel* func_to_TAC(struct FunctionDclr* declaration);

struct TACInstr* block_to_TAC(struct Slice* func_name, struct Block* block);

struct TACInstr* local_dclr_to_TAC(struct Slice* func_name, struct Declaration* dclr);

struct TACInstr* var_dclr_to_TAC(struct Slice* func_name, struct Declaration* dclr);

struct TACInstr* stmt_to_TAC(struct Slice* func_name, struct Statement* stmt);

struct TACInstr* expr_to_TAC_convert(struct Slice* func_name, struct Expr* expr, struct Val* out_val);

struct TACInstr* expr_to_TAC(struct Slice* func_name, struct Expr* expr, struct ExprResult* result);

struct TACInstr* if_to_TAC(struct Slice* func_name, struct Expr* condition, struct Statement* if_stmt);

struct TACInstr* if_else_to_TAC(struct Slice* func_name, struct Expr* condition, struct Statement* if_stmt, struct Statement* else_stmt);

struct TACInstr* cases_to_TAC(struct Slice* label, struct CaseList* cases, struct Val* rslt);

struct TACInstr* relational_to_TAC(struct Slice* func_name,
                                          struct Expr* expr,
                                          enum BinOp op,
                                          struct Expr* left,
                                          struct Expr* right,
                                          struct ExprResult* result);

struct TACInstr* args_to_TAC(struct Slice* func_name,
                                    struct ArgList* args,
                                    struct Val** out_args,
                                    size_t* out_count);

struct TACInstr* for_init_to_TAC(struct Slice* func_name, struct ForInit* init_);

struct TACInstr* while_to_TAC(struct Slice* func_name,
                                     struct Expr* condition,
                                     struct Statement* body,
                                     struct Slice* label);

struct TACInstr* do_while_to_TAC(struct Slice* func_name,
                                        struct Statement* body,
                                        struct Expr* condition,
                                        struct Slice* label);

struct TACInstr* for_to_TAC(struct Slice* func_name,
                                   struct ForInit* init_,
                                   struct Expr* condition,
                                   struct Expr* end,
                                   struct Statement* body,
                                   struct Slice* label,
                                   struct IdentMap* idents);

// ----- Utility functions -----

void concat_TAC_instrs(struct TACInstr** old_instrs, struct TACInstr* new_instrs);

struct Val* make_temp(struct Slice* func_name, struct Type* type);

void print_static_init(const struct InitList* init);

bool compare_bodies(struct TACInstr* body1, struct TACInstr* body2);

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

static struct TACInstr* tac_find_last(struct TACInstr* instr);

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
