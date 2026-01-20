#include "identifier_resolution.h"
#include "identifier_map.h"
#include "unique_name.h"
#include "source_location.h"

#include <stdarg.h>
#include <stdio.h>

// Purpose: Resolve identifiers to unique names and validate scoping rules.
// Inputs: Traverses AST nodes produced by the parser.
// Outputs: Rewrites identifier slices and reports resolution errors.
// Invariants/Assumptions: Uses a scoped identifier stack for name lookup.

// Purpose: Global identifier stack for the current resolution pass.
// Inputs: Initialized in resolve_prog and updated on scope entry/exit.
// Outputs: Used to resolve variable and function identifiers.
// Invariants/Assumptions: Only one resolution pass runs at a time.
static struct IdentStack* global_ident_stack = NULL;

// Purpose: Emit a formatted identifier-resolution error at a source location.
// Inputs: loc points into source text; fmt is printf-style.
// Outputs: Writes a diagnostic message to stdout.
// Invariants/Assumptions: source_location_from_ptr handles NULL/unknown locations.
static void ident_error_at(const char* loc, const char* fmt, ...) {
  struct SourceLocation where = source_location_from_ptr(loc);
  const char* filename = source_filename_for_ptr(loc);
  if (where.line == 0) {
    printf("Identifier Resolution Error: ");
  } else {
    printf("Identifier Resolution Error at %s:%zu:%zu: ", filename, where.line, where.column);
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}

// Purpose: Locate the nearest identifier with linkage, ignoring local-only bindings.
// Inputs: stack is the identifier scope stack; name is the identifier to search.
// Outputs: Returns the first linkage-bearing entry found, or NULL if none exist.
// Invariants/Assumptions: Searches from innermost scope outward.
static struct IdentMapEntry* find_linkage_entry(struct IdentStack* stack,
                                                struct Slice* name) {
  for (int i = (int)stack->size - 1; i >= 0; --i) {
    struct IdentMapEntry* entry = ident_map_get(stack->maps[i], name);
    if (entry != NULL && entry->has_linkage) {
      return entry;
    }
  }
  return NULL;
}

// Purpose: Resolve identifiers for all file-scope declarations.
// Inputs: prog is the Program AST.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Initializes and destroys the global scope stack.
bool resolve_prog(struct Program* prog) {
  global_ident_stack = init_scope();

  for (struct DeclarationList* decl = prog->dclrs; decl != NULL; decl = decl->next) {
    if (!resolve_file_scope_dclr(&decl->dclr)) {
      ident_error_at(NULL, "failed to resolve declaration");
      return false;
    }
  }

  destroy_ident_stack(global_ident_stack);

  return true;
}

// Purpose: Resolve identifiers within a function call argument list.
// Inputs: args is the argument list.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Uses the current scope stack.
bool resolve_args(struct ArgList* args){
  for (struct ArgList* arg = args; arg != NULL; arg = arg->next) {
    if (!resolve_expr(arg->arg)) {
      ident_error_at(arg->arg->loc, "failed to resolve argument");
      return false;
    }
  }
  return true;
}

bool resolve_var_init(struct Initializer* init){
  switch (init->init_type) {
    case SINGLE_INIT:
      return resolve_expr(init->init.single_init);
    case COMPOUND_INIT:
      for (struct InitializerList* item = init->init.compound_init; item != NULL; item = item->next) {
        if (!resolve_var_init(item->init)) {
          ident_error_at(NULL, "failed to resolve initializer list item");
          return false;
        }
      }
      return true;
    default:
      ident_error_at(NULL, "unknown initializer type");
      return false;
  }
}

// Purpose: Resolve identifiers within an expression subtree.
// Inputs: expr is the expression to resolve.
// Outputs: Returns true on success; false on any unresolved identifier.
// Invariants/Assumptions: Identifier stack is initialized before traversal.
bool resolve_expr(struct Expr* expr) {
  switch (expr->type) {
    case ASSIGN:
      if (!resolve_expr(expr->expr.assign_expr.left)) {
        return false;
      }
      return resolve_expr(expr->expr.assign_expr.right);
    case POST_ASSIGN:
      return resolve_expr(expr->expr.post_assign_expr.expr);
    case BINARY:
      if (!resolve_expr(expr->expr.bin_expr.left)) {
        return false;
      }
      return resolve_expr(expr->expr.bin_expr.right);
    case CONDITIONAL:
      if (!resolve_expr(expr->expr.conditional_expr.condition)) {
        return false;
      }
      if (!resolve_expr(expr->expr.conditional_expr.left)) {
        return false;
      }
      return resolve_expr(expr->expr.conditional_expr.right);
    case LIT:
      return true;
    case STRING:
      return true;
    case UNARY:
      return resolve_expr(expr->expr.un_expr.expr);
    case VAR: {
      bool from_current_scope = false;
      struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, expr->expr.var_expr.name, &from_current_scope);
      if (entry != NULL) {
        // Substitute the unique name for locals so later passes can ignore scoping rules.
        expr->expr.var_expr.name = entry->entry_name;
        return true;
      } else {
        ident_error_at(expr->loc, "no declaration for variable");
        return false;
      }
    }
    case FUNCTION_CALL: {
      bool from_current_scope = false;
      struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, expr->expr.fun_call_expr.func_name, &from_current_scope);
      if (entry != NULL) {
        // Resolve through the current scope so locals can shadow functions.
        expr->expr.fun_call_expr.func_name = entry->entry_name;
        return resolve_args(expr->expr.fun_call_expr.args);
      } else {
        ident_error_at(expr->loc, "function has not been declared");
        return false;
      }
    }
    case CAST:
      return resolve_expr(expr->expr.cast_expr.expr);
    case ADDR_OF:
      return resolve_expr(expr->expr.addr_of_expr.expr);
    case DEREFERENCE:
      return resolve_expr(expr->expr.deref_expr.expr);
    case SUBSCRIPT:
      return resolve_expr(expr->expr.subscript_expr.array) &&
             resolve_expr(expr->expr.subscript_expr.index);
    case SIZEOF_EXPR:
      return resolve_expr(expr->expr.sizeof_expr.expr);
    case SIZEOF_T_EXPR:
      return true;
    default:
      ident_error_at(expr->loc, "unknown expression type");
      return false;
  }
}

// Purpose: Resolve identifiers in a local variable declaration.
// Inputs: var_dclr is the variable declaration node.
// Outputs: Returns true on success; false on redeclaration or lookup errors.
// Invariants/Assumptions: Locals may be renamed to unique slices.
bool resolve_local_var_dclr(struct VariableDclr* var_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, var_dclr->name, &from_current_scope);
  if (var_dclr->storage == EXTERN) {
    if (entry != NULL && from_current_scope) {
      if (entry->has_linkage) {
        // redeclaration with extern linkage in the same block
        return true;
      }
      ident_error_at(var_dclr->name->start, "multiple declarations for variable");
      return false;
    }

    // Bind this block-scope extern to the nearest linkage-bearing declaration.
    struct IdentMapEntry* linkage_entry = find_linkage_entry(global_ident_stack, var_dclr->name);
    struct Slice* target_name = (linkage_entry != NULL) ? linkage_entry->entry_name : var_dclr->name;
    ident_stack_insert(global_ident_stack, var_dclr->name, target_name, true);
    return true;
  }

  if (entry != NULL) {
    if (from_current_scope) {
      // already declared in this scope
      ident_error_at(var_dclr->name->start, "multiple declarations for variable");
      return false;
    } else {
      // Declared in an outer scope; create a new unique local.
      struct Slice* unique_name = make_unique(var_dclr->name);
      ident_stack_insert(global_ident_stack, var_dclr->name,
          unique_name, false);
      var_dclr->name = unique_name;
      if (var_dclr->init != NULL) {
        return resolve_var_init(var_dclr->init);
      }
      return true;
    }
  }

  // First declaration in this scope: insert and optionally resolve initializer.
  struct Slice* unique_name = make_unique(var_dclr->name);
  ident_stack_insert(global_ident_stack, var_dclr->name,
      unique_name, false);
  var_dclr->name = unique_name;
  if (var_dclr->init != NULL) {
    return resolve_var_init(var_dclr->init);
  }
  return true;
}

// Purpose: Resolve identifiers in a local declaration (var or func).
// Inputs: dclr is the declaration node.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Caller manages scope entry/exit.
bool resolve_local_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      return resolve_local_var_dclr(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      return resolve_local_func(&dclr->dclr.fun_dclr);
    default:
      ident_error_at(NULL, "unknown declaration type");
      return false;
  }
}

// Purpose: Resolve identifiers in a for-loop initializer.
// Inputs: init is the initializer node.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: A for-loop introduces its own scope.
bool resolve_for_init(struct ForInit* init) {
  switch (init->type) {
    case DCLR_INIT:
      return resolve_local_var_dclr(init->init.dclr_init);
    case EXPR_INIT:
      if (init->init.expr_init != NULL) {
        return resolve_expr(init->init.expr_init);
      } else {
        return true;
      }
    default:
      ident_error_at(NULL, "unknown for init type");
      return false;
  }
}

// Purpose: Resolve identifiers within a statement subtree.
// Inputs: stmt is the statement node.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Manages scope for compound and for statements.
bool resolve_stmt(struct Statement* stmt) {
  switch (stmt->type) {
    case RETURN_STMT:
      if (!stmt->statement.ret_stmt.expr) {
        // void return, nothing to resolve
        return true;
      }
      return resolve_expr(stmt->statement.ret_stmt.expr);
    case EXPR_STMT:
      return resolve_expr(stmt->statement.expr_stmt.expr);
    case IF_STMT:
      if (!resolve_expr(stmt->statement.if_stmt.condition)) {
        return false;
      }
      if (!resolve_stmt(stmt->statement.if_stmt.if_stmt)) {
        return false;
      }
      if (stmt->statement.if_stmt.else_stmt != NULL) {
        if (!resolve_stmt(stmt->statement.if_stmt.else_stmt)) {
          return false;
        }
      }
      return true;
    case LABELED_STMT:
      return resolve_stmt(stmt->statement.labeled_stmt.stmt);
    case GOTO_STMT:
      return true;
    case COMPOUND_STMT:
      // New scope for block-local declarations.
      enter_scope(global_ident_stack);
      if (!resolve_block(stmt->statement.compound_stmt.block)) {
        return false;
      }
      exit_scope(global_ident_stack);
      return true;
    case BREAK_STMT:
      return true;
    case CONTINUE_STMT:
      return true;
    case WHILE_STMT:
      if (!resolve_expr(stmt->statement.while_stmt.condition)) {
        return false;
      }
      return resolve_stmt(stmt->statement.while_stmt.statement);
    case DO_WHILE_STMT:
      if (!resolve_stmt(stmt->statement.do_while_stmt.statement)) {
        return false;
      }
      return resolve_expr(stmt->statement.do_while_stmt.condition);
    case FOR_STMT:
      // The for-init may introduce new locals, so give the loop its own scope.
      enter_scope(global_ident_stack);
      if (!resolve_for_init(stmt->statement.for_stmt.init)) {
        return false;
      }

      if (stmt->statement.for_stmt.condition != NULL && 
          !resolve_expr(stmt->statement.for_stmt.condition)) {
        return false;
      }

      if (stmt->statement.for_stmt.end != NULL && 
          !resolve_expr(stmt->statement.for_stmt.end)) {
        return false;
      }

      if (!resolve_stmt(stmt->statement.for_stmt.statement)) {
        return false;
      }
      exit_scope(global_ident_stack);
      return true;
    case SWITCH_STMT:
      if (!resolve_expr(stmt->statement.switch_stmt.condition)) {
        return false;
      }
      return resolve_stmt(stmt->statement.switch_stmt.statement);
    case CASE_STMT:
      return resolve_stmt(stmt->statement.case_stmt.statement);
    case DEFAULT_STMT:
      return resolve_stmt(stmt->statement.default_stmt.statement);
    case NULL_STMT:
      return true;
    default:
      ident_error_at(stmt->loc, "unknown statement type");
      return false;
  }
}

// Purpose: Resolve identifiers for a local function declaration.
// Inputs: func_dclr is the function declaration node.
// Outputs: Returns true on success; false on invalid linkage or body use.
// Invariants/Assumptions: Local function declarations must be extern-only.
bool resolve_local_func(struct FunctionDclr* func_dclr) {
  // local functions must have extern linkage
  if (func_dclr->storage == STATIC) {
      ident_error_at(func_dclr->name->start,
                     "local function declarations cannot be static");
      return false;
  }

  // local functions cannot have bodies
  if (func_dclr->body != NULL) {
    ident_error_at(func_dclr->name->start, "local function cannot have body");
    return false;
  }

  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, func_dclr->name, &from_current_scope);
  if (entry != NULL && from_current_scope && !entry->has_linkage) {
    ident_error_at(func_dclr->name->start,
                   "function declaration conflicts with existing local identifier");
    return false;
  }

  if (entry == NULL || !from_current_scope) {
    // Insert into the current scope so inner blocks can shadow outer identifiers.
    ident_stack_insert(global_ident_stack, func_dclr->name,
        func_dclr->name, true);
  }

  return true;
}

// Purpose: Resolve identifiers for each parameter in a parameter list.
// Inputs: params is the parameter list.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Parameters are resolved as local variables.
bool resolve_params(struct ParamList* params){
  for (struct ParamList* param = params; param != NULL; param = param->next) {
    if (!resolve_local_var_dclr(&param->param)) {
      ident_error_at(param->param.name->start, "failed to resolve parameter");
      return false;
    }
  }
  return true;
}

// Purpose: Resolve identifiers within a block list.
// Inputs: block is the block list.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: Does not automatically enter/exit scope.
bool resolve_block(struct Block* block){
  for (struct Block* item = block; item != NULL; item = item->next) {
    switch (item->item->type) {
      case STMT_ITEM:
        if (!resolve_stmt(item->item->item.stmt)) {
          ident_error_at(item->item->item.stmt->loc, "failed to resolve statement in block");
          return false;
        }
        break;
      case DCLR_ITEM:
        if (!resolve_local_dclr(item->item->item.dclr)) {
          ident_error_at(NULL, "failed to resolve declaration in block");
          return false;
        }
        break;
      default:
        ident_error_at(NULL, "unknown block item type");
        return false;
    }
  }
  return true;
}

// Purpose: Resolve identifiers in a file-scope variable declaration.
// Inputs: var_dclr is the variable declaration node.
// Outputs: Returns true on success; false on invalid redeclarations.
// Invariants/Assumptions: File-scope variables keep their original names.
bool resolve_file_scope_var_dclr(struct VariableDclr* var_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, var_dclr->name, &from_current_scope);
  if (entry != NULL) {
    if (!from_current_scope) {
      // this should never happen, as file scope declarations are global
      ident_error_at(var_dclr->name->start, "declaration is outside file scope");
      return false;
    }
    
    // already declared
    return true;
  } else {
    // add to ident map
    ident_stack_insert(global_ident_stack, var_dclr->name,
        var_dclr->name, var_dclr->storage != STATIC);
    // expr should be a constant if it exists
    // no need to recursively process
    return true;
  }
}

// Purpose: Resolve identifiers in a file-scope function declaration/definition.
// Inputs: func_dclr is the function declaration node.
// Outputs: Returns true on success; false on invalid redeclarations.
// Invariants/Assumptions: Function bodies get their own scope for params/locals.
bool resolve_file_scope_func(struct FunctionDclr* func_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, func_dclr->name, &from_current_scope);
  if (entry != NULL) {
    if (!from_current_scope) {
      // this should never happen, as file scope declarations are global
      ident_error_at(func_dclr->name->start, "declaration is outside file scope");
      return false;
    }

    // Internal linkage functions can be declared multiple times; reuse the entry.

    if (func_dclr->body == NULL) {
      // just a declaration
      return true;
    }

    // definition after a prior declaration; resolve params/body in a new scope
    enter_scope(global_ident_stack);
    bool params_resolved = resolve_params(func_dclr->params);
    bool block_resolved = resolve_block(func_dclr->body);
    exit_scope(global_ident_stack);
    return params_resolved && block_resolved;
  } else {
    // add to ident map
    ident_stack_insert(global_ident_stack, func_dclr->name,
        func_dclr->name, func_dclr->storage != STATIC);

    // Parameters and body share a new scope distinct from file scope.
    enter_scope(global_ident_stack);
    bool params_resolved = resolve_params(func_dclr->params);
    bool block_resolved = resolve_block(func_dclr->body);
    exit_scope(global_ident_stack);

    return params_resolved && block_resolved;
  }
}

// Purpose: Resolve identifiers in a file-scope declaration.
// Inputs: dclr is the declaration node.
// Outputs: Returns true on success; false on any resolution error.
// Invariants/Assumptions: File-scope declarations share one global scope.
bool resolve_file_scope_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      return resolve_file_scope_var_dclr(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      return resolve_file_scope_func(&dclr->dclr.fun_dclr);
    default:
      printf("Identifier Resolution Error: Unknown declaration type\n");
      return false;
  }
}
