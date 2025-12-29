#include "identifier_resolution.h"
#include "identifier_map.h"

#include <stdio.h>

static struct IdentStack* global_ident_stack = NULL;
static int unique_id_counter = 0;

bool resolve_args(struct ArgList* args){
  for (struct ArgList* arg = args; arg != NULL; arg = arg->next) {
    if (!resolve_expr(&arg->arg)) {
      printf("Identifier Resolution Error: Failed to resolve argument\n");
      return false;
    }
  }
  return true;
}

/*
resolveExpr :: Expr -> MapState Expr
resolveExpr expr = case expr of
  Assign left right -> do
    rsltLeft <- resolveExpr left
    rsltRight <- resolveExpr right
    return (Assign rsltLeft rsltRight)
  PostAssign expr' op -> do
    rsltExpr <- resolveExpr expr'
    return (PostAssign rsltExpr op)
  Binary PlusEqOp left right -> do
    rsltLeft <- resolveExpr left
    rsltRight <- resolveExpr right
    return (Binary PlusEqOp rsltLeft rsltRight)
  Binary MinusEqOp left right -> do
    rsltLeft <- resolveExpr left
    rsltRight <- resolveExpr right
    return (Binary MinusEqOp rsltLeft rsltRight)
  Binary op left right -> do
    rsltLeft <- resolveExpr left
    rsltRight <- resolveExpr right
    return (Binary op rsltLeft rsltRight)
  Conditional condition left right -> do
    rsltCondition <- resolveExpr condition
    rsltLeft <- resolveExpr left
    rsltRight <- resolveExpr right
    return (Conditional rsltCondition rsltLeft rsltRight)
  Lit n -> return (Lit n)
  Unary op expr' -> Unary op <$> resolveExpr expr'
  Var name -> do
    maps <- getFst
    case lookup name maps of
      Just (MapEntry newName _ _) -> return (Var newName)
      Nothing -> lift (Err $ "Semantics Error: No declaration for variable " ++ show name)
  FunctionCall name args -> do
    maps <- getFst
    case lookup name maps of
      Just entry -> do
        newArgs <- resolveArgs args
        return (FunctionCall (entryName entry) newArgs)
      Nothing -> lift (Err $ "Semantics Error: Function " ++ show name ++ " has not been declared")
  Cast target expr' -> Cast target <$> resolveExpr expr'
  AddrOf expr' -> AddrOf <$> resolveExpr expr'
  Dereference expr' -> Dereference <$> resolveExpr expr'
  Subscript left right -> Subscript <$> resolveExpr left <*> resolveExpr right
*/
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
    case UNARY:
      return resolve_expr(expr->expr.un_expr.expr);
    case VAR: {
      bool from_current_scope = false;
      struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, expr->expr.var_expr.name, &from_current_scope);
      if (entry != NULL) {
        expr->expr.var_expr.name = entry->entry_name;
        return true;
      } else {
        printf("Identifier Resolution Error: No declaration for variable\n");
        return false;
      }
    }
    case FUNCTION_CALL: {
      bool from_current_scope = false;
      struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, expr->expr.fun_call_expr.func_name, &from_current_scope);
      if (entry != NULL) {
        expr->expr.fun_call_expr.func_name = entry->entry_name;
        return resolve_args(expr->expr.fun_call_expr.args);
      } else {
        printf("Identifier Resolution Error: Function has not been declared\n");
        return false;
      }
    }
    case CAST:
      return resolve_expr(expr->expr.cast_expr.expr);
    case ADDR_OF:
      return resolve_expr(expr->expr.addr_of_expr.expr);
    case DEREFERENCE:
      return resolve_expr(expr->expr.deref_expr.expr);
    default:
      printf("Identifier Resolution Error: Unknown expression type\n");
      return false;
  }
}

/*
resolveLocalVarDclr :: VariableDclr -> MapState VariableDclr
resolveLocalVarDclr (VariableDclr name type_ mStorage mInit) = do
  maps <- getFst
  case lookup name maps of
    (Just (MapEntry _ True True)) ->
      case mStorage of
        Just Extern -> return (VariableDclr name type_ mStorage mInit)
          -- previous declaration has linkage and so does this one; we're good
        _ -> lift (Err $ "Semantics Error: Multiple declarations for variable " ++ show name)
    (Just (MapEntry _ True False)) ->
      lift (Err $ "Semantics Error: Multiple declarations for variable " ++ show name)
    (Just (MapEntry _ False _)) ->
      case mStorage of
        Just Extern -> do
          let newMaps = replace name (MapEntry name True True) maps
          putFst newMaps
          return (VariableDclr name type_ mStorage mInit) -- no need to resolve mExpr, extern => it's a constant 
        _ -> do
          newName <- makeUnique name
          let newMaps = replace name (MapEntry newName True False) maps
          putFst newMaps
          newInit <- liftMaybe resolveVarInit mInit
          return (VariableDclr newName type_ mStorage newInit)
    Nothing ->
      case mStorage of
        Just Extern -> do
          let newMaps = (name, MapEntry name True False) : maps
          putFst newMaps
          return (VariableDclr name type_ mStorage mInit) -- no need to resolve mExpr, extern => it's a constant 
        _ -> do
          newName <- makeUnique name
          let newMaps = (name, MapEntry newName True False) : maps
          putFst newMaps
          newInit <- liftMaybe resolveVarInit mInit
          return (VariableDclr newName type_ mStorage newInit)
*/
bool resolve_local_var_dclr(struct VariableDclr* var_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, var_dclr->name, &from_current_scope);
  if (entry != NULL) {
    if (from_current_scope) {
      // already declared in this scope
      printf("Identifier Resolution Error: Multiple declarations for variable\n");
      return false;
    } else {
      // declared in outer scope
      if (var_dclr->storage == EXTERN) {
        // ok to redeclare as extern
        return true;
      } else {
        // need to create a new unique name
        char unique_name_buf[64];
        snprintf(unique_name_buf, sizeof(unique_name_buf), "%.*s_%d", 
            (int)var_dclr->name->length, var_dclr->name->data, unique_id_counter++);
        struct Slice* unique_name = create_slice_from_cstr(unique_name_buf);
        ident_stack_insert(global_ident_stack, unique_name,
            unique_name, var_dclr->storage != STATIC);
        var_dclr->name = unique_name;
        return true;
      }
    }
  } else {
    // not declared yet
    if (var_dclr->storage == EXTERN) {
      ident_stack_insert(global_ident_stack, var_dclr->name,
          var_dclr->name, true);
      return true;
    } else {
      char unique_name_buf[64];
      snprintf(unique_name_buf, sizeof(unique_name_buf), "%.*s_%d", 
          (int)var_dclr->name->length, var_dclr->name->data, unique_id_counter++);
      struct Slice* unique_name = create_slice_from_cstr(unique_name_buf);
      ident_stack_insert(global_ident_stack, unique_name,
          unique_name, var_dclr->storage != STATIC);
      var_dclr->name = unique_name;
      return true;
    }
  }
}

bool resolve_local_dclr(struct Declaration* dclr) {
  switch (dclr->type) {
    case VAR_DCLR:
      return resolve_local_var_dclr(&dclr->dclr.var_dclr);
    case FUN_DCLR:
      return resolve_local_func(&dclr->dclr.fun_dclr);
    default:
      printf("Identifier Resolution Error: Unknown declaration type\n");
      return false;
  }
}

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
      printf("Identifier Resolution Error: Unknown for init type\n");
      return false;
  }
}

bool resolve_stmt(struct Statement* stmt) {
  switch (stmt->type) {
    case RETURN_STMT:
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
      enter_scope(global_ident_stack);
      // skipping init, condition, end resolution for identifier resolution
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
      // TODO: ensure expr is constant
      return resolve_stmt(stmt->statement.case_stmt.statement);
    case DEFAULT_STMT:
      return resolve_stmt(stmt->statement.default_stmt.statement);
    case NULL_STMT:
      return true;
    default:
      printf("Identifier Resolution Error: Unknown statement type\n");
      return false;
  }
}

bool resolve_local_func(struct FunctionDclr* func_dclr) {
  // local functions must have extern linkage
  if (func_dclr->storage == STATIC) {
      printf("Identifier Resolution Error: local functions declarations cannot be static\n");
      return false;
  }

  // local functions cannot have bodies
  if (func_dclr->body != NULL) {
    printf("Identifier Resolution Error: Local function cannot have body\n");
    return false;
  }

  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, func_dclr->name, &from_current_scope);
  if (entry == NULL) {
    // add to ident map
    ident_stack_insert(global_ident_stack, func_dclr->name,
        func_dclr->name, true);
  }

  return true;
}

bool resolve_params(struct ParamList* params){
  for (struct ParamList* param = params; param != NULL; param = param->next) {
    if (!resolve_local_var_dclr(&param->param)) {
      printf("Identifier Resolution Error: Failed to resolve parameter\n");
      return false;
    }
  }
  return true;
}

bool resolve_block(struct Block* block){
  for (struct Block* item = block; item != NULL; item = item->next) {
    switch (item->item->type) {
      case STMT_ITEM:
        if (!resolve_stmt(item->item->item.stmt)) {
          printf("Identifier Resolution Error: Failed to resolve statement in block\n");
          return false;
        }
        break;
      case DCLR_ITEM:
        if (!resolve_local_dclr(item->item->item.dclr)) {
          printf("Identifier Resolution Error: Failed to resolve statement in block\n");
          return false;
        }
        break;
      default:
        printf("Identifier Resolution Error: Unknown block item type\n");
        return false;
    }
  }
  return true;
}

bool resolve_file_scope_var_dclr(struct VariableDclr* var_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, var_dclr->name, &from_current_scope);
  if (entry != NULL) {
    if (!from_current_scope) {
      // this should never happen, as file scope declarations are global
      printf("Identifier Resolution Error: Function is outside file scope\n");
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

bool resolve_file_scope_func(struct FunctionDclr* func_dclr) {
  bool from_current_scope = false;
  struct IdentMapEntry* entry = ident_stack_get(global_ident_stack, func_dclr->name, &from_current_scope);
  if (entry != NULL) {
    if (!from_current_scope) {
      // this should never happen, as file scope declarations are global
      printf("Identifier Resolution Error: Function is outside file scope\n");
      return false;
    }

    if (!entry->has_linkage) {
      // multiple declarations
      printf("Identifier Resolution Error: Multiple declarations for function\n");
      return false;
    }

    if (func_dclr->body == NULL) {
      // just a declaration
      return true;
    }

    // already defined
    return resolve_block(func_dclr->body);
  } else {
    // add to ident map
    ident_stack_insert(global_ident_stack, func_dclr->name,
        func_dclr->name, func_dclr->storage != STATIC);

    enter_scope(global_ident_stack);
    bool params_resolved = resolve_params(func_dclr->params);
    bool block_resolved = resolve_block(func_dclr->body);
    exit_scope(global_ident_stack);

    return params_resolved && block_resolved;
  }
}

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

// resolve each declaration in the program
bool resolve_prog(struct Program* prog) {
  global_ident_stack = init_scope();

  for (struct DeclarationList* decl = prog->dclrs; decl != NULL; decl = decl->next) {
    if (!resolve_file_scope_dclr(&decl->dclr)) {
      printf("Identifier Resolution Error: Failed to resolve declaration\n");
      return false;
    }
  }

  return true;
}

