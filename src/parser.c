#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "AST.h"
#include "arena.h"
#include "parser.h"
#include "token.h"
#include "token_array.h"

static struct Token * program;
static size_t prog_size;
static struct Token * current;
static struct Arena* parser_arena;

static void* parser_alloc(size_t size) {
  void* ptr = arena_alloc(parser_arena, size);
  if (ptr == NULL) {
    fprintf(stderr, "Parser memory error\n");
    exit(1);
  }
  return ptr;
}

static void print_error() {
  printf("parser failed at offset %ld\n", (size_t)(current-program));
}

static bool consume(const enum TokenType expected) {
  if (current - program < prog_size && expected == current->type) {
    current++;
    return true;
  } else {
    return false;
  }
}

static union TokenVariant* consume_with_data(const enum TokenType expected) {
  if (current - program < prog_size && expected == current->type) {
    current++;
    return &((current - 1)->data);
  } else {
    return NULL;
  }
}

static enum UnOp consume_unary_op(){
  if (consume(TILDE)) return COMPLEMENT;
  if (consume(MINUS)) return NEGATE;
  if (consume(EXCLAMATION)) return BOOL_NOT;
  return 0;
}

static enum BinOp consume_binary_op(){
  if (consume(PLUS)) return ADD_OP;
  if (consume(MINUS)) return SUB_OP;
  if (consume(ASTERISK)) return MUL_OP;
  if (consume(SLASH)) return DIV_OP;
  if (consume(PERCENT)) return MOD_OP;
  if (consume(AMPERSAND)) return BIT_AND;
  if (consume(PIPE)) return BIT_OR;
  if (consume(CARAT)) return BIT_XOR;
  if (consume(DOUBLE_AMPERSAND)) return BOOL_AND;
  if (consume(DOUBLE_PIPE)) return BOOL_OR;
  if (consume(DOUBLE_EQUALS)) return BOOL_EQ;
  if (consume(NOT_EQUAL)) return BOOL_NEQ;
  if (consume(LESS_THAN)) return BOOL_LE;
  if (consume(LESS_THAN_EQ)) return BOOL_LEQ;
  if (consume(GREATER_THAN)) return BOOL_GE;
  if (consume(GREATER_THAN_EQ)) return BOOL_GEQ;
  if (consume(EQUALS)) return ASSIGN_OP;
  if (consume(PLUS_EQ)) return PLUS_EQ_OP;
  if (consume(MINUS_EQ)) return MINUS_EQ_OP;
  if (consume(TIMES_EQ)) return MUL_EQ_OP;
  if (consume(DIV_EQ)) return DIV_EQ_OP;
  if (consume(MOD_EQ)) return MOD_EQ_OP;
  if (consume(AND_EQ)) return AND_EQ_OP;
  if (consume(OR_EQ)) return OR_EQ_OP;
  if (consume(XOR_EQ)) return XOR_EQ_OP;
  if (consume(SHL_EQ)) return SHL_EQ_OP;
  if (consume(SHR_EQ)) return SHR_EQ_OP;
  if (consume(QUESTION)) return TERNARY_OP;
  return 0;
}

struct Expr* parse_paren_var(){
  struct Expr* expr;
  if ((expr = parse_var())) return expr;
  else if (consume(OPEN_P)){
    struct Token* old_current = current - 1;
    struct Expr* inner = parse_paren_var();
    if (inner == NULL) {
      current = old_current;
      return NULL;
    } else if (!consume(CLOSE_P)){
      current = old_current;
      return NULL;
    }
    return inner;
  } else return NULL;
}

struct Expr* parse_pre_op(){
  if (consume(INC_TOK)){
    struct Expr* inner = parse_paren_var();
    if (inner == NULL) {
      current--;
      return NULL;
    }
    union ConstVariant const_data = {.int_val = 1};
    struct LitExpr one = {INT_CONST, const_data};
    struct Expr* lit_expr = parser_alloc(sizeof(struct Expr));
    lit_expr->type = LIT;
    lit_expr->expr.lit_expr = one;

    struct BinaryExpr add_one = {PLUS_EQ_OP, inner, lit_expr};

    struct Expr* result = parser_alloc(sizeof(struct Expr));
    result->type = BINARY;
    result->expr.bin_expr = add_one;
    return result;
  } else if (consume(DEC_TOK)){
    struct Expr* inner = parse_paren_var();
    if (inner == NULL) {
      current--;
      return NULL;
    }
    union ConstVariant const_data = {.int_val = 1};
    struct LitExpr one = {INT_CONST, const_data};
    struct Expr* lit_expr = parser_alloc(sizeof(struct Expr));
    lit_expr->type = LIT;
    lit_expr->expr.lit_expr = one;

    struct BinaryExpr sub_one = {MINUS_EQ_OP, inner, lit_expr};

    struct Expr* result = parser_alloc(sizeof(struct Expr));
    result->type = BINARY;
    result->expr.bin_expr = sub_one;
    return result;
  } else {
    return NULL;
  }
}

struct Expr* parse_var(){
  union TokenVariant* data;
  if ((data = consume_with_data(IDENT))){
    struct VarExpr var_expr = { data->ident_name };

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->expr.var_expr = var_expr;
    expr->type = VAR;
    return expr;
  } else return NULL;
}

enum TypeSpecifier parse_type_spec(){
  if (consume(INT_TOK)) return INT_SPEC;
  if (consume(SIGNED_TOK)) return SINT_SPEC;
  if (consume(UNSIGNED_TOK)) return UINT_SPEC;
  if (consume(LONG_TOK)) return LONG_SPEC;
  else return 0;
}

struct TypeSpecList* parse_type_specs(){
  enum TypeSpecifier spec = parse_type_spec();
  if (spec == 0) return NULL;
  struct TypeSpecList* specs = parser_alloc(sizeof(struct TypeSpecList));
  specs->spec = spec;
  specs->next = parse_type_specs();
  return specs;
}

bool spec_list_contains(struct TypeSpecList* types, enum TypeSpecifier spec){
  if (types->spec == spec) return true;
  else if (types->next == NULL) return false;
  else return spec_list_contains(types->next, spec);
}

bool spec_list_has_duplicates(struct TypeSpecList* types){
  unsigned num_ints = 0;
  unsigned num_uints = 0;
  unsigned num_sints = 0;
  unsigned num_longs = 0;
  struct TypeSpecList* cur = types;
  while (cur != NULL){
    switch (cur->spec){
      case INT_SPEC:
        num_ints++;
        break;
      case UINT_SPEC:
        num_uints++;
        break;
      case SINT_SPEC:
        num_sints++;
        break;
      case LONG_SPEC:
        num_longs++;
        break;
    }
    cur = cur->next;
  }
  if (num_ints > 1) return true;
  if (num_uints > 1) return true;
  if (num_sints > 1) return true;
  if (num_longs > 1) return true;
  return false;
}

struct Type* parse_param_type(){
  struct TypeSpecList* types = parse_type_specs();
  if (types == NULL) return NULL;
  else if (spec_list_has_duplicates(types)) {
    printf("Parse Error: Duplicate type specifiers");
    return NULL;
  } else if (spec_list_contains(types, SINT_SPEC) &&
             spec_list_contains(types, UINT_SPEC)){
    printf("Parse Error: Invalid type specifiers");
    return NULL;
  } else if (spec_list_contains(types, UINT_SPEC)){
    // ignoring long types for now
    struct Type* type = parser_alloc(sizeof(struct Type));
    type->type = UINT_TYPE;
    return type;
  } else {
    // ignoring long types for now
    struct Type* type = parser_alloc(sizeof(struct Type));
    type->type = INT_TYPE;
    return type;
  }
}

struct AbstractDeclarator* parse_direct_abstract_declarator(){
  struct Token* old_current = current;
  if (!consume(OPEN_P)) return NULL;
  struct AbstractDeclarator* declarator = parse_abstract_declarator();
  if (declarator == NULL || !consume(CLOSE_P)){
    current = old_current;
    return NULL;
  }
  return declarator;
}

struct AbstractDeclarator* parse_abstract_declarator(){
  struct Token* old_current = current;
  if (consume(ASTERISK)){
    struct AbstractDeclarator* declarator = parse_abstract_declarator();
    if (declarator == NULL){
      current = old_current;
      return NULL;
    }
    struct AbstractDeclarator* result = parser_alloc(sizeof(struct AbstractDeclarator));
    result->type = ABSTRACT_POINTER;
    result->data = declarator;
    return result;
  }
  struct AbstractDeclarator* declarator = parse_direct_abstract_declarator();
  if (declarator != NULL){
    return declarator;
  } else {
    struct AbstractDeclarator* declarator = parser_alloc(sizeof(struct AbstractDeclarator));
    declarator->type = ABSTRACT_BASE;
    return declarator;
  }
}

struct Type* process_abstract_declarator(
    struct AbstractDeclarator* declarator, 
    struct Type* base_type){
  struct Type* result;
  switch (declarator->type){
    case ABSTRACT_BASE:
      result = base_type;
      break;
    case ABSTRACT_POINTER:
      struct Type* ptr_type = parser_alloc(sizeof(struct Type));
      ptr_type->type = POINTER_TYPE;
      ptr_type->type_data.pointer_type.referenced_type = base_type;
      result = process_abstract_declarator(declarator->data, ptr_type);
      break;
  }
  return result;
}

struct Expr* parse_cast(){
  struct Token* old_current = current;
  if (!consume(OPEN_P)) return NULL;
  struct Type* base_type = parse_param_type();
  if (base_type == NULL){
    current = old_current;
    return NULL;
  }
  struct AbstractDeclarator* declarator = parse_abstract_declarator();
  if (declarator == NULL || !consume(CLOSE_P)){
    current = old_current;
    return NULL;
  }
  struct Expr* expr = parse_expr();
  if (expr == NULL){
    current = old_current;
    return NULL;
  }
  struct Type* derived_type = process_abstract_declarator(declarator, base_type);

  struct CastExpr cast = {derived_type, expr};
  struct Expr* result = parser_alloc(sizeof(struct Expr));
  result->type = CAST;
  result->expr.cast_expr = cast;
  return result;
}

struct Expr* parse_post_op(){
  struct Token* old_current = current;
  struct Expr* inner;
  if ((inner = parse_paren_var())){
    if (consume(INC_TOK)){
      struct PostAssignExpr add_one = {POST_INC, inner};
      struct Expr* result = parser_alloc(sizeof(struct Expr));
      result->type = POST_ASSIGN;
      result->expr.post_assign_expr = add_one;
      return result;
    } else if (consume(DEC_TOK)){
      struct PostAssignExpr sub_one = {POST_DEC, inner};
      struct Expr* result = parser_alloc(sizeof(struct Expr));
      result->type = POST_ASSIGN;
      result->expr.post_assign_expr = sub_one;
      return result;
    } else {
      current = old_current;
      return NULL;
    }
  } else return NULL;
}

struct Expr* parse_parens(){
  if (consume(OPEN_P)){
    struct Token* old_current = current - 1;
    struct Expr* inner = parse_expr();
    if (inner == NULL) {
      current = old_current;
      return NULL;
    } else if (!consume(CLOSE_P)){
      current = old_current;
      return NULL;
    }
    return inner;
  } else {
    return NULL;
  }
}

struct ArgList* parse_args(){
  struct Expr* arg;
  struct Token* old_current = current;
  if ((arg = parse_expr())){
    struct ArgList* args = parser_alloc(sizeof(struct ArgList));
    args->arg = *arg;
    if (consume(COMMA)) args->next = parse_args();
    else if (consume(CLOSE_P)) args->next = NULL;
    else {
      current = old_current;
      return NULL;
    }
    return args;
  } else return NULL;
}

struct Expr* parse_func_call(){
  union TokenVariant* data;
  struct Token* old_current = current;
  if ((data = consume_with_data(IDENT))){
    if (!consume(OPEN_P)){
      current = old_current;
      return NULL;
    }
    struct ArgList* args;
    if (consume(CLOSE_P)) args = NULL;
    else {
      args = parse_args();
      if (args == NULL){
        current = old_current;
        return NULL;
      }
    }
    struct FunctionCallExpr call = {data->ident_name, args};

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->type = FUNCTION_CALL;
    expr->expr.fun_call_expr = call;
    return expr;
  } else return NULL;
}

struct Expr* parse_unary(){
  enum UnOp op;
  struct Token* old_current = current;
  if ((op = consume_unary_op())){
    struct Expr* inner = parse_factor();
    if (inner == NULL) {
      current = old_current - 1;
      return NULL;
    }
    struct UnaryExpr expr = {op, inner};
    struct Expr* result = parser_alloc(sizeof(struct Expr));
    result->expr.un_expr = expr;
    result->type = UNARY;
    return result;
  }

  struct Expr* expr;
  if ((expr = parse_pre_op())){
    return expr;
  } else if (consume(ASTERISK)){ 
    struct Token* old_current = current - 1;
    struct Expr* inner = parse_expr();
    if (inner == NULL) {
      current = old_current;
      return NULL;
    }
    struct DereferenceExpr expr = {inner};
    struct Expr* result = parser_alloc(sizeof(struct Expr));
    result->expr.deref_expr = expr;
    result->type = DEREFERENCE;
    return result;
  } else if (consume(AMPERSAND)){
    struct Token* old_current = current - 1;
    struct Expr* inner = parse_expr();
    if (inner == NULL) {
      current = old_current;
      return NULL;
    }
    struct AddrOfExpr expr = {inner};
    struct Expr* result = parser_alloc(sizeof(struct Expr));
    result->expr.addr_of_expr = expr;
    result->type = ADDR_OF;
    return result;
  }
  return NULL;
}

struct Expr* parse_factor(){
  union TokenVariant* data;
  if ((data = consume_with_data(INT_LIT))){
    union ConstVariant const_data = {.int_val = data->int_val};
    struct LitExpr lit_expr = {INT_CONST, const_data};

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->expr.lit_expr = lit_expr;
    expr->type = LIT;
    return expr;
  } else if ((data = consume_with_data(U_INT_LIT))){
    union ConstVariant const_data = {.uint_val = data->uint_val};
    struct LitExpr lit_expr = {UINT_CONST, const_data};

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->expr.lit_expr = lit_expr;
    expr->type = LIT;
    return expr;
  } else if ((data = consume_with_data(LONG_LIT))){
    union ConstVariant const_data  = {.long_val = data->long_val};
    struct LitExpr lit_expr = {LONG_CONST, const_data};

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->expr.lit_expr = lit_expr;
    expr->type = LIT;
    return expr;
  } else if ((data = consume_with_data(U_LONG_LIT))){
    union ConstVariant const_data = {.ulong_val = data->ulong_val};
    struct LitExpr lit_expr = {U_LONG_LIT, const_data};

    struct Expr* expr = parser_alloc(sizeof(struct Expr));
    expr->expr.lit_expr = lit_expr;
    expr->type = LIT;
    return expr;
  }
  
  struct Expr* expr;
  if ((expr = parse_cast())) return expr;
  else if ((expr = parse_unary())) return expr;
  else if ((expr = parse_post_op())) return expr;
  else if ((expr = parse_parens())) return expr;
  else if ((expr = parse_func_call())) return expr;
  else if ((expr = parse_var())) return expr;
  else return NULL;
}

static unsigned get_prec(enum BinOp op){
  switch (op){
    case DIV_OP:
    case MUL_OP:
    case MOD_OP:
      return 50;
    case ADD_OP:
    case SUB_OP:
      return 45;
    case BIT_SHL:
    case BIT_SHR:
      return 40;
    case BOOL_LE:
    case BOOL_GE:
    case BOOL_LEQ:
    case BOOL_GEQ:
      return 35;
    case BOOL_EQ:
    case BOOL_NEQ:
      return 30;
    case BIT_AND:
      return 25;
    case BIT_XOR:
      return 20;
    case BIT_OR:
      return 15;
    case BOOL_AND:
      return 10;
    case BOOL_OR:
      return 5;
    case TERNARY_OP:
      return 3;
    case ASSIGN_OP:
    case PLUS_EQ_OP:
    case MINUS_EQ_OP:
    case MUL_EQ_OP:
    case DIV_EQ_OP:
    case MOD_EQ_OP:
    case AND_EQ_OP:
    case OR_EQ_OP:
    case XOR_EQ_OP:
    case SHL_EQ_OP:
    case SHR_EQ_OP:
      return 1;
  }
  return 0;
}

// pratt parsing W
struct Expr* parse_bin_expr(unsigned min_prec){
  struct Token* old_current = current;
  struct Expr* lhs = parse_factor();

  if (lhs == NULL) return NULL;

  while (current - program < prog_size){ 
    enum BinOp op = consume_binary_op();

    if (op == 0) {
      return lhs; 
    } 

    unsigned prec = get_prec(op);

    if (prec < min_prec) {
      current--;
      break; // stop expansion if a lower precedence operator is encountered
    }

    if (op == TERNARY_OP){
      struct Expr* middle = parse_expr();
      if (middle == NULL) {
        current = old_current;
        return NULL;
      }
      if (!consume(COLON)){
        return NULL;
      }
      struct Expr* rhs = parse_bin_expr(prec);
      if (rhs == NULL) {
        current = old_current;
        return NULL;
      }
      struct ConditionalExpr conditional_expr = {lhs, middle, rhs};
      lhs = parser_alloc(sizeof(struct Expr));
      lhs->type = CONDITIONAL;
      lhs->expr.conditional_expr = conditional_expr;
      continue;
    }

    // assignment/compound ops are right-associative, everything else is left-associative
    unsigned next_prec = (ASSIGN_OP <= op && op <= SHR_EQ_OP) ? prec : prec + 1;
    struct Expr* rhs = parse_bin_expr(next_prec);

    if (rhs == NULL){
      current--;
      return NULL;
    }

    if (op == ASSIGN_OP){
      struct AssignExpr assign_expr = {lhs, rhs};
      lhs = parser_alloc(sizeof(struct Expr));
      lhs->type = ASSIGN;
      lhs->expr.assign_expr = assign_expr;
    } else {
      struct BinaryExpr bin_expr = {op, lhs, rhs};
      lhs = parser_alloc(sizeof(struct Expr));
      lhs->type = BINARY;
      lhs->expr.bin_expr = bin_expr;
    }
  }

  return lhs;
}

struct Expr* parse_expr(){
  return parse_bin_expr(0);
}


struct Statement* parse_return_stmt(){
  struct Token* old_current = current;
  if (!consume(RETURN_TOK)) return NULL;
  struct Expr* expr = parse_expr();
  if (expr == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(SEMI)){
    current = old_current;
    return NULL;
  }
  struct ReturnStmt ret_stmt = {expr, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = RETURN_STMT;
  result->statement.ret_stmt = ret_stmt;
  return result;
}

struct Statement* parse_expr_stmt(){
  struct Token* old_current = current;
  struct Expr* expr = parse_expr();
  if (expr == NULL) return NULL;
  if (!consume(SEMI)){
    current = old_current;
    return NULL;
  }
  struct ExprStmt expr_stmt = {expr};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = EXPR_STMT;
  result->statement.expr_stmt = expr_stmt;
  return result;
}

struct Statement* parse_if_stmt(){
  struct Token* old_current = current;
  if (!consume(IF_TOK)) return NULL;
  if (!consume(OPEN_P)){
    current = old_current;
    return NULL;
  }
  struct Expr* condition = parse_expr();
  if (condition == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(CLOSE_P)) {
    current = old_current;
    return NULL;
  }
  struct Statement* left = parse_statement();
  if (left == NULL) {
    current = old_current;
    return NULL;
  }
  struct IfStmt if_stmt = {condition, left, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = IF_STMT;
  result->statement.if_stmt = if_stmt;
  old_current = current;
  if (consume(ELSE_TOK)){
    struct Statement* right = parse_statement();
    if (right == NULL){
      current = old_current;
      return NULL;
    } else {
      result->statement.if_stmt.else_stmt = right;
    }
  }
  return result;
}

struct Statement* parse_labeled_stmt(){
  struct Token* old_current = current;
  union TokenVariant* data = consume_with_data(IDENT);
  if (data == NULL) return NULL;
  struct Slice* label = data->ident_name;
  if (!consume(COLON)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }

  struct LabeledStmt labeled_stmt = {label, stmt};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = LABELED_STMT;
  result->statement.labeled_stmt = labeled_stmt;
  return result;
}

struct Statement* parse_goto_stmt(){
  struct Token* old_current = current;
  if (!consume(GOTO_TOK)) return NULL;
  union TokenVariant* data;
  if ((data = consume_with_data(IDENT)) && consume(SEMI)){
    struct GotoStmt goto_stmt = { data->ident_name };

    struct Statement* result = parser_alloc(sizeof(struct Statement));
    result->type = GOTO_STMT;
    result->statement.goto_stmt = goto_stmt;
    return result;
  } else {
    current = old_current;
    return NULL; 
  }
}

struct BlockItem* parse_block_item(){
  struct Statement* stmt = parse_statement();
  if (stmt != NULL){
    struct BlockItem* item = parser_alloc(sizeof(struct BlockItem));
    item->type = STMT_ITEM;
    item->item.stmt = stmt;
    return item;
  }
  struct Declaration* dclr = parse_declaration();
  if (dclr != NULL){
    struct BlockItem* item = parser_alloc(sizeof(struct BlockItem));
    item->type = DCLR_ITEM;
    item->item.dclr = dclr;
    return item;
  }
  return NULL; 
}

struct Block* parse_block(bool* success){
  *success = true;
  struct Token* old_current = current;

  if (!consume(OPEN_B)) {
    *success = false;
    return NULL;
  }

  struct Block* block = NULL;

  struct BlockItem* item = parse_block_item();

  if (item != NULL){
    block = parser_alloc(sizeof(struct Block));
    block->item = item;
    block->next = NULL;
    struct Block* prev_block = block;
    struct Block* cur_block;
    while ((item = parse_block_item()) != NULL){
      cur_block = parser_alloc(sizeof(struct Block));
      cur_block->item = item;
      cur_block->next = NULL;
      prev_block->next = cur_block;
      prev_block = cur_block;
    }
  }

  if (!consume(CLOSE_B)){
    current = old_current;
    *success = false;
    return NULL;
  }
  return block;
}

struct Statement* parse_compound_stmt(){
  bool success;
  // output parameter because returning NULL could mean failure or empty block
  struct Block* block = parse_block(&success);
  if (!success) return NULL;
  struct CompoundStmt compound_stmt = { block };
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = COMPOUND_STMT;
  result->statement.compound_stmt = compound_stmt;
  return result;
}

struct Statement* parse_break_stmt(){
  struct Token* old_current = current;
  if (consume(BREAK_TOK) && consume(SEMI)){
    struct BreakStmt break_stmt = {NULL};
    struct Statement* result = parser_alloc(sizeof(struct Statement));
    result->type = BREAK_STMT;
    result->statement.break_stmt = break_stmt;
    return result;
  } else {
    current = old_current;
    return NULL;
  }
}

struct Statement* parse_continue_stmt(){
  struct Token* old_current = current;
  if (consume(CONTINUE_TOK) && consume(SEMI)){
    struct ContinueStmt continue_stmt = {NULL};
    struct Statement* result = parser_alloc(sizeof(struct Statement));
    result->type = CONTINUE_STMT;
    result->statement.continue_stmt = continue_stmt;
    return result;
  } else {
    current = old_current;
    return NULL;
  }
}

struct Statement* parse_while_stmt(){
  struct Token* old_current = current;
  if (!consume(WHILE_TOK)) return NULL;
  if (!consume(OPEN_P)){
    current = old_current;
    return NULL;
  }
  struct Expr* condition = parse_expr();
  if (condition == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(CLOSE_P)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }

  struct WhileStmt while_stmt = {condition, stmt, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = WHILE_STMT;
  result->statement.while_stmt = while_stmt;
  return result;
}

struct Statement* parse_do_while_stmt(){
  struct Token* old_current = current;
  if (!consume(DO_TOK)) return NULL;
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(WHILE_TOK)){
    current = old_current;
    return NULL;
  }
  if (!consume(OPEN_P)){
    current = old_current;
    return NULL;
  }
  struct Expr* condition = parse_expr();
  if (condition == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(CLOSE_P) || !consume(SEMI)){
    current = old_current;
    return NULL;
  }

  struct DoWhileStmt do_while_stmt = {stmt, condition, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = DO_WHILE_STMT;
  result->statement.do_while_stmt = do_while_stmt;
  return result;
}

struct VariableDclr* parse_for_dclr(){
  struct Token* old_current = current;
  struct Type* type = parse_param_type();
  if (type == NULL) return NULL;
  union TokenVariant* data = consume_with_data(IDENT);
  if (data == NULL) {
    current = old_current;
    return NULL;
  }
  struct Slice* name = data->ident_name;
  struct Expr* expr = NULL;
  if (consume(EQUALS)){
    expr = parse_expr();
    if (expr == NULL){
      current = old_current;
      return NULL;
    }
  }
  if (!consume(SEMI)) {
    current = old_current;
    return NULL;
  }
  struct VariableDclr* var_dclr = parser_alloc(sizeof(struct VariableDclr));
  var_dclr->name = name;
  var_dclr->init = expr;
  var_dclr->type = type;
  var_dclr->storage = NONE;
  return var_dclr;
}

struct ForInit* parse_for_init(){
  struct Token* old_current = current;
  struct VariableDclr* var_dclr = parse_for_dclr();
  if (var_dclr != NULL){
    struct ForInit* init = parser_alloc(sizeof(struct ForInit));
    init->type = DCLR_INIT;
    init->init.dclr_init = var_dclr;
    return init;
  } else {
    struct Expr* expr_init = parse_expr();
    if (!consume(SEMI)){
      current = old_current;
      return NULL;
    }
    struct ForInit* init = parser_alloc(sizeof(struct ForInit));
    init->type = EXPR_INIT;
    init->init.expr_init = expr_init;
    return init;
  }
}

struct Statement* parse_for_stmt(){
  struct Token* old_current = current;
  if (!consume(FOR_TOK)) return NULL;
  if (!consume(OPEN_P)){
    current = old_current;
    return NULL;
  }
  struct ForInit* init = parse_for_init();
  if (init == NULL){
    current = old_current;
    return NULL;
  }
  struct Expr* condition = parse_expr();
  if (!consume(SEMI)){
    current = old_current;
    return NULL;
  }
  struct Expr* end = parse_expr();
  if (!consume(CLOSE_P)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }

  struct ForStmt for_stmt = {init, condition, end, stmt, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = FOR_STMT;
  result->statement.for_stmt = for_stmt;
  return result;
}

struct Statement* parse_switch_stmt(){
  struct Token* old_current = current;
  if (!consume(SWITCH_TOK)) return NULL;
  if (!consume(OPEN_P)){
    current = old_current;
    return NULL;
  }
  struct Expr* condition = parse_expr();
  if (condition == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(CLOSE_P)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }
  struct SwitchStmt switch_stmt = {condition, stmt, NULL, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = SWITCH_STMT;
  result->statement.switch_stmt = switch_stmt;
  return result;
}

struct Statement* parse_case_stmt(){
  struct Token* old_current = current;
  if (!consume(CASE_TOK)) return NULL;
  struct Expr* expr = parse_expr();
  if (expr == NULL){
    current = old_current;
    return NULL;
  }
  if (!consume(COLON)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }
  struct CaseStmt case_stmt = {expr, stmt, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = CASE_STMT;
  result->statement.case_stmt = case_stmt;
  return result;
}

struct Statement* parse_default_stmt(){
  struct Token* old_current = current;
  if (!consume(DEFAULT_TOK)) return NULL;
  if (!consume(COLON)){
    current = old_current;
    return NULL;
  }
  struct Statement* stmt = parse_statement();
  if (stmt == NULL){
    current = old_current;
    return NULL;
  }
  struct DefaultStmt default_stmt = {stmt, NULL};
  struct Statement* result = parser_alloc(sizeof(struct Statement));
  result->type = DEFAULT_STMT;
  result->statement.default_stmt = default_stmt;
  return result;
}

struct Statement* parse_null_stmt(){
  if (consume(SEMI)){
    struct NullStmt null_stmt;
    struct Statement* result = parser_alloc(sizeof(struct Statement));
    result->type = NULL_STMT;
    result->statement.null_stmt = null_stmt;
    return result;
  } else return NULL;
}

struct Statement* parse_statement(){
  struct Statement* stmt;
  if ((stmt = parse_return_stmt())) return stmt;
  else if ((stmt = parse_expr_stmt())) return stmt;
  else if ((stmt = parse_if_stmt())) return stmt;
  else if ((stmt = parse_labeled_stmt())) return stmt;
  else if ((stmt = parse_goto_stmt())) return stmt;
  else if ((stmt = parse_compound_stmt())) return stmt;
  else if ((stmt = parse_break_stmt())) return stmt;
  else if ((stmt = parse_continue_stmt())) return stmt;
  else if ((stmt = parse_while_stmt())) return stmt;
  else if ((stmt = parse_do_while_stmt())) return stmt;
  else if ((stmt = parse_for_stmt())) return stmt;
  else if ((stmt = parse_switch_stmt())) return stmt;
  else if ((stmt = parse_case_stmt())) return stmt;
  else if ((stmt = parse_default_stmt())) return stmt;
  else if ((stmt = parse_null_stmt())) return stmt;
  else return NULL;
}

struct Statement* parse_test(struct TokenArray* arr, struct Arena* arena){
  if (arena == NULL) {
    fprintf(stderr, "Parser requires an arena\n");
    return NULL;
  }
  parser_arena = arena;
  program = arr->tokens;
  current = program;
  prog_size = arr->size;
  struct Statement* result = parse_statement();
  if (result == NULL) {
    print_error();
    return NULL;
  }
  if (current - program != prog_size) {
    print_error();
    return NULL;
  }
  return result;
}

struct VariableDclr* parse_var_dclr(struct Type* type, enum StorageClass storage, struct Slice* name){
  struct Token* old_current = current;
  struct Expr* expr = NULL;
  if (consume(EQUALS)) {
    expr = parse_expr();
    if (expr == NULL){
      current = old_current;
      return NULL;
    }
  }
  struct VariableDclr* var_dclr = parser_alloc(sizeof(struct VariableDclr));
  var_dclr->init = expr;
  var_dclr->name = name;
  var_dclr->type = type;
  var_dclr->storage = storage;
  return var_dclr;
}

struct DclrPrefix* parse_type_or_storage_class(){
  if (consume(INT_TOK)) {
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = TYPE_PREFIX;
    dclr_prefix->prefix.type_spec = INT_SPEC;
    return dclr_prefix;
  }
  else if (consume(SIGNED_TOK)) {
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = TYPE_PREFIX;
    dclr_prefix->prefix.type_spec = SINT_SPEC;
    return dclr_prefix;
  }
  else if (consume(UNSIGNED_TOK)) {
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = TYPE_PREFIX;
    dclr_prefix->prefix.type_spec = UINT_SPEC;
    return dclr_prefix;
  }
  else if (consume(LONG_TOK)) {
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = TYPE_PREFIX;
    dclr_prefix->prefix.type_spec = LONG_SPEC;
    return dclr_prefix;
  }
  else if (consume(STATIC_TOK)){
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = STORAGE_PREFIX;
    dclr_prefix->prefix.type_spec = STATIC;
    return dclr_prefix;
  }
  else if (consume(EXTERN_TOK)){
    struct DclrPrefix* dclr_prefix = parser_alloc(sizeof(struct DclrPrefix));
    dclr_prefix->type = STORAGE_PREFIX;
    dclr_prefix->prefix.type_spec = EXTERN;
    return dclr_prefix;
  }
  else return NULL;
}

void parse_types_and_storage_classes(struct StorageClassList** storages_result, struct TypeSpecList** type_specs_result){
  struct DclrPrefix* prefix;
  struct TypeSpecList* specs = NULL;
  struct StorageClassList* storages = NULL;

  struct TypeSpecList* prev_specs = NULL;
  struct StorageClassList* prev_storages = NULL;
  while ((prefix = parse_type_or_storage_class())){
    switch(prefix->type){
      case STORAGE_PREFIX:
        struct StorageClassList* next_storage = parser_alloc(sizeof(struct StorageClassList));
        next_storage->spec = prefix->prefix.storage_class;
        next_storage->next = NULL;
        if (prev_storages != NULL){
          prev_storages->next = next_storage;
        }
        prev_storages = next_storage;
        break;
      case TYPE_PREFIX:
        struct TypeSpecList* next_spec = parser_alloc(sizeof(struct TypeSpecList));
        next_spec->spec = prefix->prefix.type_spec;
        next_spec->next = NULL;
        if (prev_specs != NULL){
          prev_specs->next = next_spec;
        }
        prev_specs = next_spec;
        break;
    }
  }
  *storages_result = storages;
  *type_specs_result = specs;
}

void parse_type_and_storage_class(struct Type** type, enum StorageClass* class){
  struct StorageClassList* storages = NULL;
  struct TypeSpecList* specs = NULL;
  parse_types_and_storage_classes(&storages, &specs);
  enum StorageClass storage = (storages == NULL) ? NONE : storages->spec;
  if (spec_list_has_duplicates(specs)){
    printf("Parse Error: duplicate type specifiers");
    *type = NULL;
    *class = NONE;
    return;
  } else if (specs == NULL){
    // ?????
    *type = NULL;
    *class = NONE;
    return;
  } else if (storages != NULL && storages->next != NULL){
    printf("Parse Error: invalid storage class");
    *type = NULL;
    *class = NONE;
    return;
  } else if (spec_list_contains(specs, SINT_SPEC) &&
             spec_list_contains(specs, UINT_SPEC)){
    printf("Parse Error: invalid type specifiers");
    *type = NULL;
    *class = NONE;
    return;
  } else if (spec_list_contains(specs, UINT_SPEC) &&
             spec_list_contains(specs, LONG_SPEC)){
    *type = parser_alloc(sizeof(struct Type));
    (*type)->type = ULONG_TYPE;
    *class = storage;
    return;
  } else if (spec_list_contains(specs, UINT_SPEC)){
    *type = parser_alloc(sizeof(struct Type));
    (*type)->type = UINT_TYPE;
    *class = storage;
    return;
  } else if (spec_list_contains(specs, LONG_SPEC)) {
    *type = parser_alloc(sizeof(struct Type));
    (*type)->type = LONG_TYPE;
    *class = storage;
    return;
  } else {
    *type = parser_alloc(sizeof(struct Type));
    (*type)->type = INT_TYPE;
    *class = storage;
    return;
  }
}

struct Declarator* parse_declarator(){
  struct Token* old_current = current;
  if (consume(ASTERISK)){
    struct Declarator* decl = parse_declarator();
    if (decl != NULL){
      struct Declarator* result = parser_alloc(sizeof(struct Declarator));
      result->type = POINTER_DEC;
      result->declarator.pointer_dec.decl = decl;
      return result;
    } else {
      current = old_current;
      return NULL;
    }
  }
  return parse_direct_declarator();
}

struct Declarator* parse_simple_declarator(){
  struct Token* old_current = current;
  union TokenVariant* data = consume_with_data(IDENT);
  if (data != NULL){
    // function or variable name
    struct Declarator* result = parser_alloc(sizeof(struct Declarator));
    result->type = IDENT_DEC;
    result->declarator.ident_dec.name = data->ident_name;
    return result;
  }
  if (consume(OPEN_P)){
    struct Declarator* result = parse_declarator();
    if (result == NULL){
      current = old_current;
      return NULL;
    }
    if (!consume(CLOSE_P)){
      current = old_current;
      return NULL;
    }
    return result;
  } 
  return NULL;
}

/*
parseParams :: Parser Token [ParamInfo]
parseParams = char OpenP *>
    ([] <$ char Void <* char CloseP <|> -- parses f(void) -- parses f(void)
     -- parses f(void)
    [] <$ char CloseP <|>               -- parses f()
    some parseParam)                    -- parses f([params]) 

parseParam :: Parser Token ParamInfo
parseParam = do
  type_ <- parseParamType
  declarator <- parseDeclarator <* (char Comma <|> char CloseP)
  return (Param type_ declarator)
*/
struct ParamInfoList* parse_params(){

}

struct Declarator* parse_direct_declarator(){
  struct Declarator* decl = parse_simple_declarator();
  if (decl == NULL) return NULL;
  struct ParamInfoList* params = parse_params();
  if (params == NULL) return decl;
  struct FunDec fun_dec = {params, decl};
  struct Declarator* result = parser_alloc(sizeof(struct Declarator));
  result->type = FUN_DEC;
  result->declarator.fun_dec = fun_dec;
  return result;
}

/*
-- converts declarators and base types into actual types
processDeclarator :: Declarator -> Type_ -> Result (String, Type_, [VariableDclr])
processDeclarator decl baseType = case decl of
  IdentDec name -> Ok (name, baseType, [])
  PointerDec d -> processDeclarator d (PointerType baseType)
  FunDec params d -> case d of
    IdentDec name -> do
      (paramNames, types) <- unzip <$> processParamsInfo params
      let derivedType = FunType types baseType
      -- convert list of names and types to list of 'VariableDclr's
      let dclrs = getZipList $ (\n t -> VariableDclr n t Nothing Nothing)
                  <$> ZipList paramNames <*> ZipList types
      return (name, derivedType, dclrs)
    _ -> Err "Parse Error: Can't apply additional type derivations to a function type"

processParamInfo :: ParamInfo -> Result (String, Type_)
processParamInfo (Param type_ d) = do
  case d of
    FunDec _ _ -> Err "Parse Error: Function pointers in parameters aren't supported"
    _ -> return ()
  (paramName, paramType_, _) <- processDeclarator d type_
  return (paramName, paramType_)

-- converts declarators and base types into actual types, 
-- but for function parameters
processParamsInfo :: [ParamInfo] -> Result [(String, Type_)]
processParamsInfo =
  foldr (\p ps -> case processParamInfo p of
  Ok p' -> liftA2 (:) (Ok p') ps
  Err s -> Err s
  Fail -> Fail) (Ok [])
*/
void process_declarator(){

}

/*
parseEndOfFunc :: Parser Token (Maybe Block)
parseEndOfFunc = do
  body <- optional parseBlock
  case body of
    Just _ -> return body -- function definition
    Nothing -> Nothing <$ char Semi -- function declaration
*/

struct ParamTypeList* params_to_types(struct ParamList* params){

}

struct Block* parse_end_of_func(bool* success){
  bool* success2;
  struct Block* body = parse_block(success2);

}

struct FunctionDclr* parse_function(struct Type* ret_type, enum StorageClass storage, 
                                    struct Slice* name, struct ParamList* params){
  struct Type* fun_type = parser_alloc(sizeof(struct Type));
  fun_type->type = FUN_TYPE;
  fun_type->type_data.fun_type.return_type = ret_type;
  fun_type->type_data.fun_type.param_types = params_to_types(params);
  struct FunctionDclr* result = parser_alloc(sizeof(struct FunctionDclr));
  result->name = name;
  result->params = params;
  result->storage = storage;
  bool* success;
  result->body = parse_end_of_func(success);
  if (success){

  } else {
    
  }
  return result;
}

/*
parseDclr :: Parser Token Declaration
parseDclr = do
  -- base type and storage specifiers are the first part of a declaration
  (baseType, mStorage) <- parseTypeAndStorageClass
  -- next is the declarator
  declarator <- parseDeclarator
  -- base type and declarator must be parsed to work out the derived type
  (name, declType, params) <-
    case processDeclarator declarator baseType of
      Ok x -> return x
      Err msg -> errorParse msg
      Fail -> failParse
  -- the rest of the declaration is either a function body or expression
  case declType of
    FunType _ retType -> FunDclr <$> parseFunction retType mStorage name params
    _ -> VarDclr <$> parseVariableDclr declType mStorage name
*/
struct Declaration* parse_declaration(){
  return NULL;
}

struct Program* parse_prog(struct TokenArray* arr){
  // parse declarations and put them in a linked list
  program = arr->tokens;
  current = program;
  prog_size = arr->size;

  struct Program* prog = parser_alloc(sizeof(struct Program));
  struct DeclarationList* dclrs = parser_alloc(sizeof(struct DeclarationList));

  prog->dclrs = dclrs;

  struct DeclarationList* current_dclr = dclrs;

  struct Declaration* dclr = parse_declaration();

  if (dclr == NULL) {
    // there were 0 declarations
    prog->dclrs = NULL;
    return prog;
  }

  while (dclr != NULL){
    // TODO: this order is wrong
    struct DeclarationList* next_dclr = parser_alloc(sizeof(struct DeclarationList));
    next_dclr->dclr = *dclr;
    next_dclr->next = NULL;

    current_dclr->next = next_dclr;
    current_dclr = next_dclr;

    dclr = parse_declaration();
  }

  return prog;
}
