#include "typechecking.h"
#include "arena.h"
#include "source_location.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

// Purpose: Implement typechecking and symbol table utilities.
// Inputs: Operates on AST nodes produced by parsing and resolution.
// Outputs: Annotates expressions with types and validates declarations.
// Invariants/Assumptions: Typechecker uses a single global symbol table.

// Purpose: Global symbol table for the current typechecking pass.
// Inputs: Initialized in typecheck_program and used by all helpers.
// Outputs: Stores symbol entries for declarations and lookups.
// Invariants/Assumptions: Only one typechecking pass runs at a time.
struct SymbolTable* global_symbol_table = NULL;

// Purpose: Identify compound assignment operators.
// Inputs: op is the binary operator enum value.
// Outputs: Returns true for +=, -=, etc.
// Invariants/Assumptions: ASSIGN_OP is handled separately.
static bool is_compound_assign_op(enum BinOp op) {
  switch (op) {
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
      return true;
    default:
      return false;
  }
}

// Purpose: Map a compound assignment operator to its base binary operator.
// Inputs: op must satisfy is_compound_assign_op(op).
// Outputs: Returns the corresponding arithmetic/bitwise operator.
// Invariants/Assumptions: Caller validates op.
static enum BinOp compound_assign_base_op(enum BinOp op) {
  switch (op) {
    case PLUS_EQ_OP:
      return ADD_OP;
    case MINUS_EQ_OP:
      return SUB_OP;
    case MUL_EQ_OP:
      return MUL_OP;
    case DIV_EQ_OP:
      return DIV_OP;
    case MOD_EQ_OP:
      return MOD_OP;
    case AND_EQ_OP:
      return BIT_AND;
    case OR_EQ_OP:
      return BIT_OR;
    case XOR_EQ_OP:
      return BIT_XOR;
    case SHL_EQ_OP:
      return BIT_SHL;
    case SHR_EQ_OP:
      return BIT_SHR;
    default:
      return op;
  }
}

// Purpose: Emit a formatted type error at a source location.
// Inputs: loc points into source text; fmt is printf-style.
// Outputs: Writes a diagnostic message to stdout.
// Invariants/Assumptions: source_location_from_ptr handles NULL/unknown locations.
static void type_error_at(const char* loc, const char* fmt, ...) {
  struct SourceLocation where = source_location_from_ptr(loc);
  const char* filename = source_filename_for_ptr(loc);
  if (where.line == 0) {
    printf("Type error: ");
  } else {
    printf("Type error at %s:%zu:%zu: ", filename, where.line, where.column);
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}

// Purpose: Normalize a static initializer value to the target type width/sign.
// Inputs: value holds raw bits; target is the destination type.
// Outputs: Returns the normalized value for storage in InitValue.
// Invariants/Assumptions: Uses two's-complement sign extension for signed types.
static uint64_t normalize_static_init(uint64_t value, struct Type* target) {
  size_t size = get_type_size(target);
  if (size == 0) {
    return value;
  }
  size_t bits = size * 8;
  uint64_t mask = (bits >= 64) ? UINT64_MAX : ((UINT64_C(1) << bits) - 1);
  uint64_t truncated = value & mask;
  if (is_signed_type(target) && bits < 64) {
    uint64_t sign_bit = UINT64_C(1) << (bits - 1);
    if (truncated & sign_bit) {
      truncated |= ~mask;
    }
  }
  return truncated;
}

// Purpose: Extract a literal value from an initializer expression.
// Inputs: expr is the initializer expression (literal or cast of a literal).
// Outputs: Returns true and writes out_value on success.
// Invariants/Assumptions: Only literal initializers are supported here.
static bool init_literal_value(struct Expr* expr, uint64_t* out_value) {
  if (expr == NULL || out_value == NULL) {
    return false;
  }
  struct Expr* cur = expr;
  if (cur->type == CAST) {
    cur = cur->expr.cast_expr.expr;
  }
  if (cur->type != LIT) {
    return false;
  }
  struct LitExpr* lit = &cur->expr.lit_expr;
  switch (lit->type) {
    case INT_CONST:
      *out_value = (uint64_t)(int64_t)lit->value.int_val;
      return true;
    case UINT_CONST:
      *out_value = (uint64_t)lit->value.uint_val;
      return true;
    case LONG_CONST:
      *out_value = (uint64_t)(int64_t)lit->value.long_val;
      return true;
    case ULONG_CONST:
      *out_value = (uint64_t)lit->value.ulong_val;
      return true;
    default:
      return false;
  }
}

// ------------------------- Typechecking Functions ------------------------- //

// Purpose: Typecheck every declaration in a program.
// Inputs: program is the Program AST.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Initializes global_symbol_table for this pass.
bool typecheck_program(struct Program* program) {
  global_symbol_table = create_symbol_table(1024);

  // typecheck each declaration in the program
  for (struct DeclarationList* cur = program->dclrs; cur != NULL; cur = cur->next){
    if (!typecheck_file_scope_dclr(&cur->dclr)) {
      return false;
    }
  }
  return true;
}

// Purpose: Typecheck a file-scope declaration.
// Inputs: dclr is the declaration node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: File-scope symbols are stored in global_symbol_table.
bool typecheck_file_scope_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      // Type check variable declaration
      return typecheck_file_scope_var(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      // Type check function declaration
      return typecheck_func(&dclr->dclr.fun_dclr);
    default:
      type_error_at(NULL, "unknown declaration type in typecheck_file_scope_dclr");
      return false; // Unknown declaration type
  }
}

// Purpose: Typecheck a file-scope variable declaration/definition.
// Inputs: var_dclr is the variable declaration node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Global initializers must be constant literals.
bool typecheck_file_scope_var(struct VariableDclr* var_dclr) {
  enum IdentInitType init_type = -1;
  // Infer file-scope initialization status from storage class and initializer.
  if (var_dclr->init != NULL) {
    init_type = INITIAL;
  } else if (var_dclr->storage != EXTERN) {
    init_type = TENTATIVE;
  } else {
    init_type = NO_INIT;
  }

  // if there is an initializer, typecheck it
  if (var_dclr->init != NULL) {
    if (var_dclr->init->type != LIT) {
      // For simplicity, we only allow literal initializers for global variables
      type_error_at(var_dclr->init->loc,
                    "non-constant initializer for global variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    if (var_dclr->type->type == POINTER_TYPE) {
      // For simplicity, we only allow null pointer constants as initializers for global pointer variables
      struct LitExpr* lit_expr = &var_dclr->init->expr.lit_expr;
      if (lit_expr->value.int_val != 0) {
        type_error_at(var_dclr->init->loc,
                      "invalid pointer initializer for global variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }
    }

    if (!typecheck_init(&var_dclr->init, var_dclr->type)) {
      return false;
    }
  }

  struct SymbolEntry* entry = symbol_table_get(global_symbol_table, var_dclr->name);

  // check if this variable has been declared before
  if (entry != NULL) {

    // reject function types
    if (entry->type->type == FUN_TYPE) {
      type_error_at(var_dclr->name->start,
                    "function %.*s redeclared as variable",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // ensure both declarations have the same type
    if (!compare_types(entry->type, var_dclr->type)) {
      type_error_at(var_dclr->name->start,
                    "conflicting declarations for variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // check for duplicate definitions
    if (entry->attrs->init.init_type == INITIAL && var_dclr->init != NULL) {
      type_error_at(var_dclr->name->start,
                    "conflicting file scope variable definitions for variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // check for conflicting linkage (internal vs external)
    bool entry_internal = (entry->attrs->storage == STATIC);
    bool dclr_internal = (var_dclr->storage == EXTERN) ? entry_internal
                                                       : (var_dclr->storage == STATIC);
    if (entry_internal != dclr_internal) {
      type_error_at(var_dclr->name->start,
                    "conflicting variable linkage for variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // By this point, types and linkage match; update only if init state improves.
    // Ordering is NO_INIT < TENTATIVE < INITIAL.

    if (init_type > entry->attrs->init.init_type) {
      // upgrade init type
      entry->attrs->init.init_type = init_type;
      if (init_type == INITIAL) {
        entry->attrs->init.init_list = arena_alloc(sizeof(struct InitList));
        entry->attrs->init.init_list->value.int_type = get_var_init(var_dclr);
        uint64_t init_value = 0;
        if (!init_literal_value(var_dclr->init, &init_value)) {
          type_error_at(var_dclr->init->loc,
                        "non-constant initializer for global variable %.*s",
                        (int)var_dclr->name->len, var_dclr->name->start);
          return false;
        }
        entry->attrs->init.init_list->value.value =
            normalize_static_init(init_value, var_dclr->type);
        entry->attrs->is_defined = true;
        entry->attrs->init.init_list->next = NULL;
      }
    }
  } else {
    // new declaration, add to symbol table
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = STATIC_ATTR;
    attrs->is_defined = (init_type == INITIAL);
    attrs->storage = var_dclr->storage;
    attrs->init.init_type = init_type;
    if (init_type == INITIAL) {
      attrs->init.init_list = arena_alloc(sizeof(struct InitList));
      attrs->init.init_list->value.int_type = get_var_init(var_dclr);
      uint64_t init_value = 0;
      if (!init_literal_value(var_dclr->init, &init_value)) {
        type_error_at(var_dclr->init->loc,
                      "non-constant initializer for global variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }
      attrs->init.init_list->value.value = normalize_static_init(init_value, var_dclr->type);
      attrs->init.init_list->next = NULL;
    } else {
      attrs->init.init_list = NULL;
    }

    symbol_table_insert(global_symbol_table, var_dclr->name, var_dclr->type, attrs);
  }

  return true;
}
  
// Purpose: Typecheck a function declaration or definition.
// Inputs: func_dclr is the function declaration node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Parameters and body share the global symbol table.
bool typecheck_func(struct FunctionDclr* func_dclr) {
  struct SymbolEntry* entry = symbol_table_get(global_symbol_table, func_dclr->name);

  if (entry == NULL) {
    // First declaration/definition of this function.
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = FUN_ATTR;
    attrs->is_defined = (func_dclr->body != NULL);
    attrs->storage = func_dclr->storage;
    symbol_table_insert(global_symbol_table, func_dclr->name, func_dclr->type, attrs);
  } else {
    // ensure the existing entry is a function
    if (entry->type->type != FUN_TYPE) {
      type_error_at(func_dclr->name->start,
                    "variable %.*s redeclared as function",
                    (int)func_dclr->name->len, func_dclr->name->start);
      return false;
    }

    // ensure both declarations have the same type
    if (!compare_types(entry->type, func_dclr->type)) {
      type_error_at(func_dclr->name->start,
                    "conflicting declarations for function %.*s",
                    (int)func_dclr->name->len, func_dclr->name->start);
      return false;
    }

    // check for duplicate definitions
    if (entry->attrs->is_defined && func_dclr->body != NULL) {
      type_error_at(func_dclr->name->start,
                    "multiple definitions for function %.*s",
                    (int)func_dclr->name->len, func_dclr->name->start);
      return false;
    }

    // check for conflicting linkage
    // extern matches previous linkage, cannot cause conflict
    if (entry->attrs->storage != func_dclr->storage && 
       (func_dclr->storage != EXTERN) &&
       (entry->attrs->storage != EXTERN)) {
      type_error_at(func_dclr->name->start,
                    "conflicting function linkage for function %.*s",
                    (int)func_dclr->name->len, func_dclr->name->start);
      return false;
    }

    // update definition status
    if (func_dclr->body != NULL) {
      entry->attrs->is_defined = true;
    }
  }

  // Parameters share the same symbol table as the body in this pass.
  if (!typecheck_params(func_dclr->params)) {
    return false;
  }

  if (func_dclr->body != NULL) {
    // typecheck function body
    if (!typecheck_block(func_dclr->body)) {
      return false;
    }
  }

  return true;
}

// Purpose: Typecheck and register each function parameter.
// Inputs: params is the parameter list.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Parameters are inserted into global_symbol_table.
bool typecheck_params(struct ParamList* params) {
  struct ParamList* cur = params;
  while (cur != NULL) {
    // ensure each parameter has no initializer
    if (cur->param.init != NULL) {
      type_error_at(cur->param.name->start,
                    "function parameter %.*s should not have an initializer",
                    (int)cur->param.name->len, cur->param.name->start);
      return false;
    }

    // Add parameters as locals so body expressions can reference them.
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = LOCAL_ATTR;
    attrs->is_defined = true;
    attrs->storage = NONE; // parameters have no storage class
    symbol_table_insert(global_symbol_table, cur->param.name, cur->param.type, attrs);

    cur = cur->next;
  }
  return true;
}

// Purpose: Typecheck each item in a block.
// Inputs: block is the block list.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Symbol table is shared across the function body.
bool typecheck_block(struct Block* block) {
  struct Block* cur = block;
  while (cur != NULL) {
    switch (cur->item->type) {
      case DCLR_ITEM:
        // typecheck local declaration
        if (!typecheck_local_dclr(cur->item->item.dclr)) {
          return false;
        }
        break;
      case STMT_ITEM:
        // typecheck statement
        if (!typecheck_stmt(cur->item->item.stmt)) {
          return false;
        }
        break;
      default:
        type_error_at(NULL, "unknown block item type in typecheck_block");
        return false; // Unknown block item type
    }
    cur = cur->next;
  }
  return true;
}

// Purpose: Typecheck a statement subtree.
// Inputs: stmt is the statement node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Return statements reference the current function symbol.
bool typecheck_stmt(struct Statement* stmt) {
  switch (stmt->type) {
    // Placeholder implementation
    case RETURN_STMT: {
      if (!typecheck_convert_expr(stmt->statement.ret_stmt.expr)) {
        return false;
      }

      // Ensure the return value can be converted to the function's return type.
      struct SymbolEntry* entry = symbol_table_get(global_symbol_table, stmt->statement.ret_stmt.func);
      if (entry == NULL) {
        type_error_at(stmt->loc, "unknown function in return statement");
        return false;
      }
      struct Type* func_type = entry->type;
      struct Type* ret_type = func_type->type_data.fun_type.return_type;

      if (!convert_by_assignment(&stmt->statement.ret_stmt.expr, ret_type)) {
        type_error_at(stmt->loc, "incompatible return type in return statement");
        return false;
      }

      break;
    }
    case EXPR_STMT: {
      if (!typecheck_convert_expr(stmt->statement.expr_stmt.expr)) {
        return false;
      }
      break;
    }
    case IF_STMT: {
      if (!typecheck_convert_expr(stmt->statement.if_stmt.condition)) {
        return false;
      }

      if (!is_arithmetic_type(stmt->statement.if_stmt.condition->value_type) &&
          !is_pointer_type(stmt->statement.if_stmt.condition->value_type)) {
        type_error_at(stmt->statement.if_stmt.condition->loc,
                      "if condition must have scalar type");
        return false;
      }

      if (!typecheck_stmt(stmt->statement.if_stmt.if_stmt)) {
        return false;
      }
      if (stmt->statement.if_stmt.else_stmt != NULL) {
        if (!typecheck_stmt(stmt->statement.if_stmt.else_stmt)) {
          return false;
        }
      }
      break;
    }
    case GOTO_STMT: {
      // nothing to typecheck
      break;
    }
    case LABELED_STMT: {
      if (!typecheck_stmt(stmt->statement.labeled_stmt.stmt)) {
        return false;
      }
      break;
    }
    case WHILE_STMT: {
      if (!typecheck_convert_expr(stmt->statement.while_stmt.condition)) {
        return false;
      }

      if (!is_arithmetic_type(stmt->statement.while_stmt.condition->value_type) &&
          !is_pointer_type(stmt->statement.while_stmt.condition->value_type)) {
        type_error_at(stmt->statement.while_stmt.condition->loc,
                      "while condition must have scalar type");
        return false;
      }

      if (!typecheck_stmt(stmt->statement.while_stmt.statement)) {
        return false;
      }
      break;
    }
    case DO_WHILE_STMT: {
      if (!typecheck_stmt(stmt->statement.do_while_stmt.statement)) {
        return false;
      }
      if (!typecheck_convert_expr(stmt->statement.do_while_stmt.condition)) {
        return false;
      }

      if (!is_arithmetic_type(stmt->statement.do_while_stmt.condition->value_type) &&
          !is_pointer_type(stmt->statement.do_while_stmt.condition->value_type)) {
        type_error_at(stmt->statement.do_while_stmt.condition->loc,
                      "do-while condition must have scalar type");
        return false;
      }

      break;
    }
    case FOR_STMT: {
      // Each part is optional, but any present expressions must be typed.
      if (!typecheck_for_init(stmt->statement.for_stmt.init)) {
        return false;
      }
      if (stmt->statement.for_stmt.condition != NULL) {
        if (!typecheck_convert_expr(stmt->statement.for_stmt.condition)) {
          return false;
        }
      }
      if (stmt->statement.for_stmt.end != NULL) {
        if (!typecheck_convert_expr(stmt->statement.for_stmt.end)) {
          return false;
        }
      }
      if (!typecheck_stmt(stmt->statement.for_stmt.statement)) {
        return false;
      }
      break;
    }
    case SWITCH_STMT: {
      if (!typecheck_convert_expr(stmt->statement.switch_stmt.condition)) {
        return false;
      }

      if (!is_arithmetic_type(stmt->statement.switch_stmt.condition->value_type)) {
        type_error_at(stmt->statement.switch_stmt.condition->loc,
                      "switch condition must have arithmetic type");
        return false;
      }

      if (!typecheck_stmt(stmt->statement.switch_stmt.statement)) {
        return false;
      }
      break;
    }
    case CASE_STMT: {
      if (!typecheck_convert_expr(stmt->statement.case_stmt.expr)) {
        return false;
      }
      if (!typecheck_stmt(stmt->statement.case_stmt.statement)) {
        return false;
      }
      break;
    }
    case DEFAULT_STMT: {
      if (!typecheck_stmt(stmt->statement.default_stmt.statement)) {
        return false;
      }
      break;
    }
    case BREAK_STMT: {
      // nothing to typecheck
      break;
    }
    case CONTINUE_STMT: {
      // nothing to typecheck
      break;
    }
    case COMPOUND_STMT: {
      if (!typecheck_block(stmt->statement.compound_stmt.block)) {
        return false;
      }
      break;
    }
    case NULL_STMT: {
      // nothing to typecheck
      break;
    }

    default: {
      type_error_at(stmt->loc, "unknown statement type in typecheck_stmt");
      return false; // Unknown statement type
    }
  }

  return true;
}

// Purpose: Typecheck the initializer portion of a for statement.
// Inputs: init_ is the ForInit node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: For-init may be a declaration or expression.
bool typecheck_for_init(struct ForInit* init_) {
  switch (init_->type) {
    case DCLR_INIT:
      if (init_->init.dclr_init->storage != NONE) {
        type_error_at(init_->init.dclr_init->name->start,
                      "storage class not allowed in for-loop initializer for variable %.*s",
                      (int)init_->init.dclr_init->name->len,
                      init_->init.dclr_init->name->start);
        return false;
      }
      return typecheck_local_var(init_->init.dclr_init);
    case EXPR_INIT:
      if (init_->init.expr_init != NULL) {
        return typecheck_convert_expr(init_->init.expr_init);
      } else {
        return true; // Nothing to typecheck
      }
    default:
      type_error_at(NULL, "unknown for init type in typecheck_for_init");
      return false; // Unknown for init type
  }
}

// Purpose: Typecheck a local declaration (variable or function).
// Inputs: dclr is the declaration node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Local declarations use the global symbol table.
bool typecheck_local_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      return typecheck_local_var(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      return typecheck_func(&dclr->dclr.fun_dclr);
    default:
      type_error_at(NULL, "unknown declaration type in typecheck_local_dclr");
      return false; // Unknown declaration type
  }
}

// Purpose: Typecheck a local variable declaration/definition.
// Inputs: var_dclr is the variable declaration node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Enforces extern/static/local linkage rules.
bool typecheck_local_var(struct VariableDclr* var_dclr) {
  if (var_dclr->storage == EXTERN) {
    // Local extern declarations just validate and/or introduce a global symbol.
    if (var_dclr->init != NULL) {
      type_error_at(var_dclr->init->loc,
                    "initializer on local extern variable declaration for variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, var_dclr->name);
    if (entry == NULL) {
      // New extern declaration shares the global table but has no definition.
      struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
      attrs->attr_type = STATIC_ATTR;
      attrs->is_defined = false;
      attrs->storage = EXTERN;
      attrs->init.init_type = NO_INIT;
      attrs->init.init_list = NULL;
      symbol_table_insert(global_symbol_table, var_dclr->name, var_dclr->type, attrs);
    } else {
      // ensure the existing entry is not a function
      if (entry->type->type == FUN_TYPE) {
        type_error_at(var_dclr->name->start,
                      "function %.*s redeclared as variable",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }

      // ensure both declarations have the same type
      if (!compare_types(entry->type, var_dclr->type)) {
        type_error_at(var_dclr->name->start,
                      "conflicting declarations for variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }
    }

    return true;
  } else if (var_dclr->storage == STATIC) {
    // Local static behaves like a file-scope object with local visibility.
    if (var_dclr->init != NULL) {
      if (var_dclr->init->type != LIT) {
        type_error_at(var_dclr->init->loc,
                      "non-constant initializer for static local variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }
      if (var_dclr->type->type == POINTER_TYPE) {
        struct LitExpr* lit_expr = &var_dclr->init->expr.lit_expr;
        if (lit_expr->value.int_val != 0) {
          type_error_at(var_dclr->init->loc,
                        "invalid pointer initializer for static local variable %.*s",
                        (int)var_dclr->name->len, var_dclr->name->start);
          return false;
        }
      }
      if (!typecheck_init(&var_dclr->init, var_dclr->type)) {
        return false;
      }
    }

    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, var_dclr->name);
    if (entry == NULL) {
      struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
      attrs->attr_type = STATIC_ATTR;
      attrs->is_defined = (var_dclr->init != NULL);
      attrs->storage = STATIC;
      attrs->init.init_type = INITIAL;
      attrs->init.init_list = arena_alloc(sizeof(struct InitList));
      attrs->init.init_list->value.int_type = get_var_init(var_dclr);
      uint64_t init_value = 0;
      if (var_dclr->init != NULL) {
        if (!init_literal_value(var_dclr->init, &init_value)) {
          type_error_at(var_dclr->init->loc,
                        "non-constant initializer for static local variable %.*s",
                        (int)var_dclr->name->len, var_dclr->name->start);
          return false;
        }
      } else {
        init_value = 0;
      }
      attrs->init.init_list->value.value = normalize_static_init(init_value, var_dclr->type);
      attrs->init.init_list->next = NULL;
      symbol_table_insert(global_symbol_table, var_dclr->name, var_dclr->type, attrs);
    } else {
      // ensure the existing entry is not a function
      if (entry->type->type == FUN_TYPE) {
        type_error_at(var_dclr->name->start,
                      "function %.*s redeclared as variable",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }

      // ensure both declarations have the same type
      if (!compare_types(entry->type, var_dclr->type)) {
        type_error_at(var_dclr->name->start,
                      "conflicting declarations for variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }

      // check for duplicate definitions
      if (entry->attrs->is_defined && var_dclr->init != NULL) {
        type_error_at(var_dclr->name->start,
                      "conflicting local static variable definitions for variable %.*s",
                      (int)var_dclr->name->len, var_dclr->name->start);
        return false;
      }

      // update definition status
      if (var_dclr->init != NULL) {
        entry->attrs->is_defined = true;
      }
    }

    return true;
  } else {
    // Regular local variable must be unique within the current function scope.
    struct SymbolEntry* entry = symbol_table_get(global_symbol_table, var_dclr->name);
    if (entry != NULL) {
      type_error_at(var_dclr->name->start,
                    "duplicate local variable declaration for variable %.*s",
                    (int)var_dclr->name->len, var_dclr->name->start);
      return false;
    }

    // add to symbol table
    struct IdentAttr* attrs = arena_alloc(sizeof(struct IdentAttr));
    attrs->attr_type = LOCAL_ATTR;
    attrs->is_defined = true;
    attrs->storage = NONE; // local variables have no storage class
    symbol_table_insert(global_symbol_table, var_dclr->name, var_dclr->type, attrs);

    if (var_dclr->init != NULL) {
      // Allow self-references in initializers (e.g., int a = a = 5).
      if (!typecheck_init(&var_dclr->init, var_dclr->type)) {
        return false;
      }
    }
  }

  return true;
}

// Purpose: Typecheck an expression and apply conversion rules.
// Inputs: expr is the expression node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Currently delegates to typecheck_expr.
bool typecheck_convert_expr(struct Expr* expr) {
  // will do more once I implement arrays
  return typecheck_expr(expr);
}

// Purpose: Typecheck and convert an initializer expression.
// Inputs: init is the initializer pointer; type is the target type.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: May rewrite *init with a cast expression.
bool typecheck_init(struct Expr** init, struct Type* type) {
  if (*init == NULL) {
    return true; // Nothing to typecheck
  }

  if (!typecheck_convert_expr(*init)) {
    return false;
  }

  if (!convert_by_assignment(init, type)) {
    return false;
  }

  return true;
}

// Purpose: Typecheck an expression subtree and set value_type.
// Inputs: expr is the expression node.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: value_type is assigned for each expression node.
bool typecheck_expr(struct Expr* expr) {
  switch (expr->type) {
    case BINARY: {
      struct BinaryExpr* bin_expr = &expr->expr.bin_expr;
      if (!typecheck_convert_expr(bin_expr->left)) {
        return false;
      }
      if (!typecheck_convert_expr(bin_expr->right)) {
        return false;
      }

      struct Type* left_type = bin_expr->left->value_type;
      struct Type* right_type = bin_expr->right->value_type;

      if (is_compound_assign_op(bin_expr->op)) {
        if (!is_lvalue(bin_expr->left)) {
          type_error_at(expr->loc, "cannot assign to non-lvalue");
          return false;
        }

        enum BinOp base_op = compound_assign_base_op(bin_expr->op);
        if ((base_op == ADD_OP || base_op == SUB_OP) &&
            is_pointer_type(left_type) &&
            is_arithmetic_type(right_type)) {
          expr->value_type = left_type;
          return true;
        }

        if (!is_arithmetic_type(left_type) || !is_arithmetic_type(right_type)) {
          type_error_at(expr->loc, "invalid types in compound assignment");
          return false;
        }

        if (base_op == BIT_SHL || base_op == BIT_SHR) {
          convert_expr_type(&bin_expr->right, left_type);
          expr->value_type = left_type;
          return true;
        }

        struct Type* op_type = get_common_type(left_type, right_type);
        if (op_type == NULL) {
          type_error_at(expr->loc, "incompatible types in compound assignment");
          return false;
        }
        convert_expr_type(&bin_expr->right, op_type);
        expr->value_type = left_type;
        return true;
      }

      // Equality allows pointer comparisons; otherwise use arithmetic common type.
      if (bin_expr->op == BOOL_EQ || bin_expr->op == BOOL_NEQ) {
        struct Type* common_type = NULL;
        if (is_pointer_type(left_type) || is_pointer_type(right_type)) {
          common_type = get_common_pointer_type(bin_expr->left, bin_expr->right);
          if (common_type == NULL) {
            type_error_at(expr->loc, "incompatible pointer types in equality comparison");
            return false;
          }
        } else {
          common_type = get_common_type(left_type, right_type);
          if (common_type == NULL) {
            type_error_at(expr->loc, "incompatible types in equality comparison");
            return false;
          }
        }

        convert_expr_type(&bin_expr->left, common_type);
        convert_expr_type(&bin_expr->right, common_type);
        expr->value_type = arena_alloc(sizeof(struct Type));
        expr->value_type->type = INT_TYPE; // result type of equality comparison is int
        return true;
      }
      else if (bin_expr->op == ADD_OP || bin_expr->op == PLUS_EQ_OP) {
        if (is_arithmetic_type(left_type) && is_arithmetic_type(right_type)) {
          struct Type* common_type = get_common_type(left_type, right_type);
          convert_expr_type(&bin_expr->left, common_type);
          convert_expr_type(&bin_expr->right, common_type);
          expr->value_type = common_type;
          return true;
        } else if ((is_arithmetic_type(left_type) && is_pointer_type(right_type)) ||
                   (is_pointer_type(left_type) && is_arithmetic_type(right_type))) {
          // Pointer arithmetic yields a pointer.
          expr->value_type = is_pointer_type(left_type) ? left_type : right_type;
          return true;
        } else {
          type_error_at(expr->loc, "invalid types for pointer arithmetic in addition");
          return false;
        }
      }
      else if (bin_expr->op == SUB_OP || bin_expr->op == MINUS_EQ_OP) {
        if (is_arithmetic_type(left_type) && is_arithmetic_type(right_type)) {
          struct Type* common_type = get_common_type(left_type, right_type);
          convert_expr_type(&bin_expr->left, common_type);
          convert_expr_type(&bin_expr->right, common_type);
          expr->value_type = common_type;
          return true;
        } else if (is_pointer_type(left_type) && is_arithmetic_type(right_type)) {
          // Pointer minus integer yields a pointer.
          expr->value_type = left_type;
          return true;
        } else {
          type_error_at(expr->loc, "invalid types for pointer arithmetic in subtraction");
          return false;
        }
      } 
      else if (bin_expr->op == BOOL_AND || bin_expr->op == BOOL_OR) {
        // Logical operators always yield int in this language subset.
        expr->value_type = arena_alloc(sizeof(struct Type));
        expr->value_type->type = INT_TYPE; // result type of logical operations is int
        return true;
      }
      else if (bin_expr->op == BIT_SHL || bin_expr->op == BIT_SHR) {
        if (!is_arithmetic_type(left_type) || !is_arithmetic_type(right_type)) {
          type_error_at(expr->loc, "invalid types in shift expression");
          return false;
        }
        convert_expr_type(&bin_expr->right, left_type);
        expr->value_type = left_type;
        return true;
      } else {
        if (is_pointer_type(left_type) || is_pointer_type(right_type)) {
          type_error_at(expr->loc, "invalid pointer arithmetic in binary operation");
          return false;
        }
        struct Type* common_type = get_common_type(left_type, right_type);
        if (common_type == NULL) {
          type_error_at(expr->loc, "incompatible types in binary operation");
          return false;
        }
        convert_expr_type(&bin_expr->left, common_type);
        convert_expr_type(&bin_expr->right, common_type);
        expr->value_type = arena_alloc(sizeof(struct Type));
        if (bin_expr->op == BOOL_LE || bin_expr->op == BOOL_LEQ ||
            bin_expr->op == BOOL_GE || bin_expr->op == BOOL_GEQ) {
          expr->value_type->type = INT_TYPE; // result type of relational operations is int
        } else {
          expr->value_type = common_type;
        }
        return true;
      }
    }
    case ASSIGN: {
      struct AssignExpr* assign_expr = &expr->expr.assign_expr;
      if (!typecheck_convert_expr(assign_expr->left)) {
        return false;
      }

      if (!is_lvalue(assign_expr->left)) {
        type_error_at(expr->loc, "cannot assign to non-lvalue");
        return false;
      }

      if (!typecheck_convert_expr(assign_expr->right)) {
        return false;
      }

      if (!convert_by_assignment(&assign_expr->right, assign_expr->left->value_type)) {
        type_error_at(expr->loc, "incompatible types in assignment");
        return false;
      }

      expr->value_type = assign_expr->left->value_type;
      return true;
    }
    case POST_ASSIGN: {
      struct PostAssignExpr* post_assign_expr = &expr->expr.post_assign_expr;
      if (!typecheck_convert_expr(post_assign_expr->expr)) {
        return false;
      }

      if (!is_lvalue(post_assign_expr->expr)) {
        type_error_at(expr->loc, "cannot apply post-increment/decrement to non-lvalue");
        return false;
      }

      if (!is_arithmetic_type(post_assign_expr->expr->value_type) &&
          !is_pointer_type(post_assign_expr->expr->value_type)) {
        type_error_at(expr->loc,
                      "post-increment/decrement requires arithmetic or pointer type");
        return false;
      }

      expr->value_type = post_assign_expr->expr->value_type;
      return true;
    }
    case CONDITIONAL: {
      struct ConditionalExpr* cond_expr = &expr->expr.conditional_expr;
      if (!typecheck_convert_expr(cond_expr->condition)) {
        return false;
      }
      if (!typecheck_convert_expr(cond_expr->left)) {
        return false;
      }
      if (!typecheck_convert_expr(cond_expr->right)) {
        return false;
      }

      struct Type* left_type = cond_expr->left->value_type;
      struct Type* right_type = cond_expr->right->value_type;

      // Find a compatible common type for the true/false arms.
      struct Type* common_type = NULL;
      if (is_pointer_type(left_type) || is_pointer_type(right_type)) {
        common_type = get_common_pointer_type(cond_expr->left, cond_expr->right);
        if (common_type == NULL) {
          type_error_at(expr->loc, "incompatible pointer types in conditional expression");
          return false;
        }
      } else {
        common_type = get_common_type(left_type, right_type);
        if (common_type == NULL) {
          type_error_at(expr->loc, "incompatible types in conditional expression");
          return false;
        }
      }

      convert_expr_type(&cond_expr->left, common_type);
      convert_expr_type(&cond_expr->right, common_type);
      expr->value_type = common_type;
      return true;
    }
    case FUNCTION_CALL: {
      struct SymbolEntry* entry = symbol_table_get(global_symbol_table, expr->expr.fun_call_expr.func_name);
      if (entry == NULL) {
        type_error_at(expr->loc, "unknown function in function call");
        return false;
      }
      struct Type* func_type = entry->type;
      if (func_type->type != FUN_TYPE) {
        type_error_at(expr->loc,
                      "variable %.*s cannot be used as a function",
                      (int)expr->expr.fun_call_expr.func_name->len,
                      expr->expr.fun_call_expr.func_name->start);
        return false;
      }

      // Arguments are converted by assignment to each parameter type.
      struct ParamTypeList* param_types = func_type->type_data.fun_type.param_types;
      if (!typecheck_args(expr->expr.fun_call_expr.args, param_types, expr)) {
        return false;
      }
      expr->value_type = func_type->type_data.fun_type.return_type;
      return true;
    }
    case VAR: {
      struct SymbolEntry* entry = symbol_table_get(global_symbol_table, expr->expr.var_expr.name);
      if (entry == NULL) {
        type_error_at(expr->loc,
                      "unknown variable %.*s",
                      (int)expr->expr.var_expr.name->len, expr->expr.var_expr.name->start);
        return false;
      }
      if (entry->type->type == FUN_TYPE) {
        type_error_at(expr->loc,
                      "function %.*s cannot be used as a variable",
                      (int)expr->expr.var_expr.name->len, expr->expr.var_expr.name->start);
        return false;
      }
      expr->value_type = entry->type;
      return true;
    }
    case UNARY: {
      struct UnaryExpr* unary_expr = &expr->expr.un_expr;
      if (!typecheck_convert_expr(unary_expr->expr)) {
        return false;
      }
      struct Type* expr_type = unary_expr->expr->value_type;
      if (is_pointer_type(expr_type) &&
          (unary_expr->op == NEGATE || unary_expr->op == COMPLEMENT)) {
        type_error_at(expr->loc, "invalid pointer operation in unary expression");
        return false;
      }
      if (unary_expr->op == BOOL_NOT) {
        expr->value_type = arena_alloc(sizeof(struct Type));
        expr->value_type->type = INT_TYPE;
      } else {
        expr->value_type = expr_type;
      }
      return true;
    }
    case LIT: {
      struct LitExpr* lit_expr = &expr->expr.lit_expr;
      expr->value_type = arena_alloc(sizeof(struct Type));
      switch (lit_expr->type) {
        case INT_CONST:
          expr->value_type->type = INT_TYPE;
          return true;
        case UINT_CONST:
          expr->value_type->type = UINT_TYPE;
          return true;
        case LONG_CONST:
          expr->value_type->type = LONG_TYPE;
          return true;
        case ULONG_CONST:
          expr->value_type->type = ULONG_TYPE;
          return true;
        default:
          type_error_at(expr->loc, "unknown literal type in typecheck_expr");
          return false; // Unknown literal type
      }
    }
    case CAST: {
      struct CastExpr* cast_expr = &expr->expr.cast_expr;
      if (!typecheck_convert_expr(cast_expr->expr)) {
        return false;
      }
      expr->value_type = cast_expr->target;
      return true;
    }
    case ADDR_OF: {
      struct AddrOfExpr* addr_of_expr = &expr->expr.addr_of_expr;
      if (!typecheck_convert_expr(addr_of_expr->expr)) {
        return false;
      }
      if (!is_lvalue(addr_of_expr->expr)) {
        type_error_at(expr->loc, "cannot take the address of a non-lvalue");
        return false;
      }
      struct Type* referenced = addr_of_expr->expr->value_type;
      expr->value_type = arena_alloc(sizeof(struct Type));
      expr->value_type->type = POINTER_TYPE;
      expr->value_type->type_data.pointer_type.referenced_type = referenced;
      return true;
    }
    case DEREFERENCE: {
      struct DereferenceExpr* deref_expr = &expr->expr.deref_expr;
      if (!typecheck_convert_expr(deref_expr->expr)) {
        return false;
      }
      struct Type* expr_type = deref_expr->expr->value_type;
      if (!is_pointer_type(expr_type)) {
        type_error_at(expr->loc, "cannot dereference non-pointer type");
        return false;
      }
      expr->value_type = expr_type->type_data.pointer_type.referenced_type;
      return true;
    }
    default: {
      type_error_at(expr->loc, "unknown expression type in typecheck_expr");
      return false; // Unknown expression type
    }
  }
}

// Purpose: Typecheck a function call argument list.
// Inputs: args are call arguments; types are parameter types; call_site for errors.
// Outputs: Returns true on success; false on any type error.
// Invariants/Assumptions: Arguments are converted by assignment.
bool typecheck_args(struct ArgList* args, struct ParamTypeList* types, struct Expr* call_site) {
  for (; args != NULL && types != NULL; args = args->next, types = types->next) {
    if (!typecheck_convert_expr(args->arg)) {
      return false;
    }
    if (!convert_by_assignment(&args->arg, types->type)) {
      return false;
    }
  }
  if (args != NULL || types != NULL) {
    type_error_at(call_site ? call_site->loc : NULL,
                  "argument and parameter count mismatch");
    return false;
  }
  return true;
}

// ------------------------- Type Utility Functions ------------------------- //

// Purpose: Check if a type is arithmetic.
// Inputs: type is the Type node.
// Outputs: Returns true for integer-like types.
// Invariants/Assumptions: Pointer and function types are not arithmetic.
bool is_arithmetic_type(struct Type* type) {
  switch (type->type) {
    case INT_TYPE:
    case UINT_TYPE:
    case LONG_TYPE:
    case ULONG_TYPE:
      return true;
    default:
      return false;
  }
}

// Purpose: Check if a type is signed.
// Inputs: type is the Type node.
// Outputs: Returns true for signed integer types.
// Invariants/Assumptions: Unsigned types return false.
bool is_signed_type(struct Type* type) {
  switch (type->type) {
    case INT_TYPE:
    case LONG_TYPE:
      return true;
    default:
      return false;
  }
}

// Purpose: Check if a type is a pointer type.
// Inputs: type is the Type node.
// Outputs: Returns true if type->type == POINTER_TYPE.
// Invariants/Assumptions: Does not inspect referenced type.
bool is_pointer_type(struct Type* type) {
  return type->type == POINTER_TYPE;
}

// Purpose: Insert a cast expression to convert to a target type.
// Inputs: expr is the expression pointer; target is the desired type.
// Outputs: Rewrites *expr if a cast is needed.
// Invariants/Assumptions: Uses arena allocation for the new cast node.
void convert_expr_type(struct Expr** expr, struct Type* target) {
  if (!compare_types((*expr)->value_type, target)) {
    struct Expr* new_expr = arena_alloc(sizeof(struct Expr));
    new_expr->loc = (*expr)->loc;
    new_expr->type = CAST;
    new_expr->expr.cast_expr.target = target;
    new_expr->expr.cast_expr.expr = *expr;
    new_expr->value_type = target;
    *expr = new_expr;
  }
}

// Purpose: Compute the size of a type in bytes.
// Inputs: type is the Type node.
// Outputs: Returns the size in bytes or 0 for unknown types.
// Invariants/Assumptions: Pointer size is treated as 4 bytes here.
size_t get_type_size(struct Type* type) {
  switch (type->type) {
    case INT_TYPE:
    case UINT_TYPE:
      return 4;
    case LONG_TYPE:
    case ULONG_TYPE:
      return 8;
    case POINTER_TYPE:
      return 4; // 32-bit architecture
    default:
      return 0; // unknown type size
  }
}

// Purpose: Determine the common arithmetic type of two operands.
// Inputs: t1 and t2 are operand types.
// Outputs: Returns a common type or NULL if incompatible.
// Invariants/Assumptions: Pointer types are not handled here.
struct Type* get_common_type(struct Type* t1, struct Type* t2) {
  if (compare_types(t1, t2)) {
    return t1;
  }

  size_t size1 = get_type_size(t1);
  size_t size2 = get_type_size(t2);

  if (size1 == size2) {
    if (is_signed_type(t1)) {
      return t2;
    } else {
      return t1;
    }
  } else if (size1 > size2) {
    return t1;
  } else {
    return t2;
  }
}

// Purpose: Determine if an expression is an lvalue.
// Inputs: expr is the expression node.
// Outputs: Returns true if assignable.
// Invariants/Assumptions: Only VAR and DEREFERENCE are lvalues here.
bool is_lvalue(struct Expr* expr) {
  switch (expr->type) {
    case VAR:
      return true;
    case DEREFERENCE:
      return true;
    default:
      return false;
  }
}

// Purpose: Check if an expression is a null pointer constant.
// Inputs: expr is the expression node.
// Outputs: Returns true for literal integer 0.
// Invariants/Assumptions: Only INT_CONST zero is treated as null.
bool is_null_pointer_constant(struct Expr* expr) {
  if (expr->type == LIT) {
    struct LitExpr* lit_expr = &expr->expr.lit_expr;
    if (lit_expr->type == INT_CONST && lit_expr->value.int_val == 0) {
      return true;
    }
  }
  return false;
}

// Purpose: Determine a common pointer type for pointer comparisons/conditionals.
// Inputs: expr1 and expr2 are the operand expressions.
// Outputs: Returns a compatible pointer type or NULL if incompatible.
// Invariants/Assumptions: Allows null pointer constants to match any pointer.
struct Type* get_common_pointer_type(struct Expr* expr1, struct Expr* expr2) {
  struct Type* t1 = expr1->value_type;
  struct Type* t2 = expr2->value_type;

  if (compare_types(t1, t2)) {
    return t1;
  } else if (is_null_pointer_constant(expr1)) {
    return t2;
  } else if (is_null_pointer_constant(expr2)) {
    return t1;
  }
  return NULL;
}

// Purpose: Apply assignment conversion rules to an expression.
// Inputs: expr is the expression pointer; target is the target type.
// Outputs: Returns true on success; false on invalid conversions.
// Invariants/Assumptions: May rewrite *expr with a cast expression.
bool convert_by_assignment(struct Expr** expr, struct Type* target) {
  if (compare_types((*expr)->value_type, target)) {
    return true;
  }

  // Apply the assignment conversion rules for arithmetic and null-pointer cases.
  if (is_arithmetic_type((*expr)->value_type) && is_arithmetic_type(target)) {
    // perform conversion
    convert_expr_type(expr, target);
    return true;
  }

  if (is_pointer_type(target) && is_null_pointer_constant(*expr)) {
    // perform conversion (TODO: should really only allow this for null pointer constants)
    convert_expr_type(expr, target);
    return true;
  }

  type_error_at((*expr)->loc, "cannot convert type for assignment");
  return false;
}

// Purpose: Map a variable declaration to a static initializer kind.
// Inputs: var_dclr is the variable declaration node.
// Outputs: Returns a StaticInitType enum value.
// Invariants/Assumptions: Only integer-like types are supported here.
enum StaticInitType get_var_init(struct VariableDclr* var_dclr) {
  switch (var_dclr->type->type) {
    case INT_TYPE:
      return INT_INIT;
    case UINT_TYPE:
      return UINT_INIT;
    case LONG_TYPE:
      return LONG_INIT;
    case ULONG_TYPE:
      return ULONG_INIT;
    // will eventaully handle array zero init
    default:
      printf("Warning: Unknown variable type for static initialization\n");
      return -1; // unknown init type
  }
}


// ------------------------- Symbol Table Functions ------------------------- //

// Purpose: Allocate a symbol table with a given bucket count.
// Inputs: numBuckets is the number of hash buckets.
// Outputs: Returns a SymbolTable allocated in the arena.
// Invariants/Assumptions: Entries are arena-allocated and persist for the pass.
struct SymbolTable* create_symbol_table(size_t numBuckets){
  struct SymbolTable* table = arena_alloc(sizeof(struct SymbolTable));
  table->size = numBuckets;
  table->arr = arena_alloc(sizeof(struct SymbolEntry*) * numBuckets);
  for (size_t i = 0; i < numBuckets; i++){
    table->arr[i] = NULL;
  }
  return table;
}

// Purpose: Insert a symbol entry into the table.
// Inputs: hmap is the table; key/type/attrs define the symbol.
// Outputs: Updates the table in place.
// Invariants/Assumptions: Does not check for duplicates.
void symbol_table_insert(struct SymbolTable* hmap, struct Slice* key, struct Type* type, struct IdentAttr* attrs){
  size_t label = hash_slice(key) % hmap->size;
  
  struct SymbolEntry* newEntry = arena_alloc(sizeof(struct SymbolEntry));
  newEntry->key = key;
  newEntry->type = type;
  newEntry->attrs = attrs;
  newEntry->next = NULL;

  if (hmap->arr[label] == NULL){
    hmap->arr[label] = newEntry;
  } else {
    struct SymbolEntry* cur = hmap->arr[label];
    while (cur->next != NULL){
      cur = cur->next;
    }
    cur->next = newEntry;
  }
}

// Purpose: Look up a symbol entry by identifier name.
// Inputs: hmap is the table; key is the identifier slice.
// Outputs: Returns the entry or NULL if missing.
// Invariants/Assumptions: hash_slice is consistent with insertions.
struct SymbolEntry* symbol_table_get(struct SymbolTable* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  struct SymbolEntry* cur = hmap->arr[label];
  while (cur != NULL){
    if (compare_slice_to_slice(cur->key, key)){
      return cur;
    }
    cur = cur->next;
  }
  return NULL;
}

// Purpose: Check if a symbol exists in the table.
// Inputs: hmap is the table; key is the identifier slice.
// Outputs: Returns true if the symbol is present.
// Invariants/Assumptions: Performs a full lookup in the bucket chain.
bool symbol_table_contains(struct SymbolTable* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  struct SymbolEntry* cur = hmap->arr[label];
  while (cur != NULL){
    if (compare_slice_to_slice(cur->key, key)){
      return true;
    }
    cur = cur->next;
  }
  return false;
}

// Purpose: Print the symbol table contents for debugging.
// Inputs: hmap is the table to print.
// Outputs: Writes a human-readable dump to stdout.
// Invariants/Assumptions: Intended for debugging only.
void print_symbol_table(struct SymbolTable* hmap){
  for (size_t i = 0; i < hmap->size; i++){
    struct SymbolEntry* cur = hmap->arr[i];
    while (cur != NULL){
      printf("Key: %.*s\n", (int)cur->key->len, cur->key->start);
      printf("  Type: ");
      print_type(cur->type);
      printf("\n");
      printf("  Attributes:\n");
      print_ident_attr(cur->attrs);
      printf("\n");
      cur = cur->next;
    }
  }
}

// Purpose: Print identifier attributes for debugging.
// Inputs: attrs is the attribute structure.
// Outputs: Writes a readable description to stdout.
// Invariants/Assumptions: Intended for debugging only.
void print_ident_attr(struct IdentAttr* attrs){
  if (attrs == NULL){
    printf("NULL\n");
    return;
  }
  printf("    Attr Type: ");
  switch (attrs->attr_type){
    case FUN_ATTR:
      printf("Function\n");
      break;
    case STATIC_ATTR:
      printf("Static Variable\n");
      print_ident_init(&attrs->init);
      break;
    case LOCAL_ATTR:
      printf("Local Variable\n");
      break;
    default:
      printf("Unknown Attribute Type\n");
      break;
  }
  printf("    Is Defined: %s\n", attrs->is_defined ? "true" : "false");
  printf("    Storage Class: ");
  switch (attrs->storage) {
    case NONE:
      printf("None\n");
      break;
    case STATIC:
      printf("Static\n");
      break;
    case EXTERN:
      printf("Extern\n");
      break;
    default:
      printf("Unknown\n");
      break;
  }
}

// Purpose: Print initializer metadata for debugging.
// Inputs: init is the initializer structure.
// Outputs: Writes a readable description to stdout.
// Invariants/Assumptions: Intended for debugging only.
void print_ident_init(struct IdentInit* init){
  if (init == NULL){
    printf("NULL\n");
    return;
  }
  printf("    Init Type: ");
  switch (init->init_type){
    case NO_INIT:
      printf("No Init\n");
      break;
    case TENTATIVE:
      printf("Tentative\n");
      break;
    case INITIAL:
      printf("Initial ");
      struct InitList* cur = init->init_list;
      if (cur->value.int_type == INT_INIT || cur->value.int_type == LONG_INIT) {
        printf("%" PRId64, (int64_t)cur->value.value);
      } else {
        printf("%" PRIu64, (uint64_t)cur->value.value);
      }
      printf("\n");
      break;
    default:
      printf("Unknown Init Type\n");
      break;
  }
}
