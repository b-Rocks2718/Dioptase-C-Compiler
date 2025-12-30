#ifndef IDENTIFIER_RESOLUTION_H
#define IDENTIFIER_RESOLUTION_H

#include <stdbool.h>
#include "AST.h"
#include "arena.h"

bool resolve_expr(struct Expr* expr);

bool resolve_local_dclr(struct Declaration* dclr);

bool resolve_file_scope_dclr(struct Declaration* dclr);

bool resolve_local_var_dclr(struct VariableDclr* var_dclr);

bool resolve_file_scope_var_dclr(struct VariableDclr* var_dclr);

bool resolve_file_scope_func(struct FunctionDclr* func_dclr);

bool resolve_local_func(struct FunctionDclr* func_dclr);

bool resolve_block(struct Block* block);

bool resolve_prog(struct Program* prog);

#endif // IDENTIFIER_RESOLUTION_H