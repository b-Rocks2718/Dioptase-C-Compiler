#ifndef TYPECHECKING_H
#define TYPECHECKING_H

#include "AST.h"
#include "slice.h"

#include <stdbool.h>
#include <stdint.h>

struct SymbolEntry{
  struct Slice* key;
  struct Type* type;
  struct IdentAttr* attrs;
  struct SymbolEntry* next;
};

struct SymbolTable{
	size_t size;
  struct SymbolEntry** arr;
};

enum IdentAttrType {
  FUN_ATTR,
  STATIC_ATTR,
  LOCAL_ATTR
};

enum IdentInitType {
  NO_INIT = 0,
  TENTATIVE = 1,
  INITIAL = 2
};

struct IdentInit {
  enum IdentInitType init_type;
  struct InitList* init_list; // valid if init_type == Initial
};

enum StaticInitType {
  INT_INIT,
  UINT_INIT,
  LONG_INIT,
  ULONG_INIT,
  ZERO_INIT
};

union InitValue {
  enum StaticInitType int_type;
  int value;
};

struct InitList {
  union InitValue value;
  struct InitList* next;
};

struct IdentAttr {
  enum IdentAttrType attr_type;
  bool is_defined;
  bool is_global;
  struct IdentInit init;
};

// ------------------------- Typechecking Functions ------------------------- //

bool typecheck_program(struct Program* program);

bool typecheck_file_scope_dclr(struct Declaration* dclr);

bool typecheck_file_scope_var(struct VariableDclr* var_dclr);

bool typecheck_func(struct FunctionDclr* func_dclr);

bool typecheck_init(struct Expr** init, struct Type* type);

bool typecheck_expr(struct Expr* expr);

bool typecheck_convert_expr(struct Expr* expr);

bool typecheck_params(struct ParamList* params);

bool typecheck_block(struct Block* block);

bool typecheck_local_dclr(struct Declaration* dclr);

bool typecheck_stmt(struct Statement* stmt);

bool typecheck_local_var(struct VariableDclr* var_dclr);

bool typecheck_for_init(struct ForInit* init_);

bool typecheck_args(struct ArgList* args, struct ParamTypeList* params, struct Expr* call_site);

// ------------------------- Type Utility Functions ------------------------- //

bool is_arithmetic_type(struct Type* type);

bool is_pointer_type(struct Type* type);

bool convert_by_assignment(struct Expr** expr, struct Type* target_type);

struct Type* get_common_type(struct Type* type1, struct Type* type2);

struct Type* get_common_pointer_type(struct Expr* expr1, struct Expr* expr2);

void convert_expr_type(struct Expr** expr, struct Type* target_type);

bool is_lvalue(struct Expr* expr);

// ------------------------- Symbol Table Functions ------------------------- //

struct SymbolTable* create_symbol_table(size_t numBuckets);

void symbol_table_insert(struct SymbolTable* hmap, struct Slice* key, struct Type* type, struct IdentAttr* attrs);

struct SymbolEntry* symbol_table_get(struct SymbolTable* hmap, struct Slice* key);

bool symbol_table_contains(struct SymbolTable* hmap, struct Slice* key);

#endif // TYPECHECKING_H
