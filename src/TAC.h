#ifndef TAC_H
#define TAC_H

#include "AST.h"

struct TACProg {
  struct TopLevel* top_levels;
};

enum TopLevelType {
    FUNC,
    STATIC_VAR,
    COMMENT
};

struct TopLevel {
  enum TopLevelType type;
  struct Slice* name;
  bool global;

  struct TACInstr* body; // for Func
  struct Slice** params; // for Func
  size_t num_params;    // for Func
  
  struct Type* var_type; // for StaticVar
  struct StaticInit* init_values; // for StaticVar
  size_t num_inits; // for StaticVar
  
  struct Slice* comment; // for Comment
  
  struct TopLevel* next;
};

enum ValType {
    CONSTANT,
    VARIABLE
};

union ValVariant {
    int const_value; // assuming constants are integers for simplicity
    char* var_name;
};

struct Val {
    enum ValType val_type;
    union ValVariant val;
};

enum TACInstrType {
    TACRETURN,
    TACUNARY,
    TACBINARY,
    TACCOND_JUMP,
    TACCMP,
    TACJUMP,
    TACLABEL,
    TACCOPY,
    TACCALL,
    TACGET_ADDRESS,
    TACLOAD,
    TACSTORE,
    TACCOPY_TO_OFFSET
};

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

struct TACReturn {
  struct Val dst;
};

struct TACUnary {
  enum UnOp op;
  struct Val dst;
  struct Val src;
};

struct TACBinary {
  enum BinOp op;
  struct Val dst;
  struct Val src1;
  struct Val src2;
  struct Type* type;
};

struct TACCondJump {
  enum TACCondition condition;
  struct Slice* label;
};

struct TACCmp {
  struct Val src1;
  struct Val src2;
};

struct TACJump {
  struct Slice* label;
};

struct TACLabel {
  struct Slice* label;
};

struct TACCopy {
  struct Val dst;
  struct Val src;
};

struct TACCall {
  struct Slice* func_name;
  struct Val dst;
  struct Val* args;
  size_t num_args;
};

struct TACGetAddress {
  struct Val dst;
  struct Val src;
};

struct TACLoad {
  struct Val dst;
  struct Val src_ptr;
};

struct TACStore {
  struct Val dst_ptr;
  struct Val src;
};

struct TACCopyToOffset {
  struct Val dst;
  struct Val src;
  int offset;
};

union TACInstrVariant {
  struct TACReturn tac_return;
  struct TACUnary tac_unary;
  struct TACBinary tac_binary;
  struct TACCondJump tac_cond_jump;
  struct TACCmp tac_cmp;
  struct TACJump tac_jump;
  struct TACLabel tac_label;
  struct TACCopy tac_copy;
  struct TACCall tac_call;
  struct TACGetAddress tac_get_address;
  struct TACLoad tac_load;
  struct TACStore tac_store;
  struct TACCopyToOffset tac_copy_to_offset;
};

struct TACInstr {
    enum TACInstrType type;
    union TACInstrVariant instr;
    struct TACInstr* next;
};

enum ExprResultType {
  PLAIN_OPERAND,
  DEREFERENCED_POINTER
};

struct ExprResult {
    enum ExprResultType type;
    struct Val val;
};

#endif // TAC_H