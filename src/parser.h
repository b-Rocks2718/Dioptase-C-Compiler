#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "token_array.h"

struct Arena;

struct Program* parse_prog(struct TokenArray* tokens, struct Arena* arena);

struct Statement* parse_statement();

struct Expr* parse_expr();

struct Expr* parse_unary();

struct Expr* parse_bin_expr();

struct Expr* parse_factor();

struct Expr* parse_var();

struct AbstractDeclarator* parse_abstract_declarator();

struct VariableDclr* parse_var_dclr();

struct Declaration* parse_declaration();

struct Declarator* parse_declarator();

struct Declarator* parse_simple_declarator();

struct Declarator* parse_direct_declarator();

#endif
