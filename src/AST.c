#include <stdio.h>
#include <stdlib.h>

#include "AST.h"
#include "slice.h"

/* show instances */

void print_tabs(unsigned tabs) {
  for (int i = 0; i < tabs; ++i) printf("    ");
}

void print_param_type_list(struct ParamTypeList* type_list){
  if (type_list == NULL) return;
  print_type(&type_list->type);
  printf(", ");
  print_param_type_list(type_list->next);
}

void print_type(struct Type* type){
  switch (type->type){
    case INT_TYPE:
      printf("int");
      break;
    case UINT_TYPE:
      printf("uint");
      break;
    case LONG_TYPE:
      printf("long");
      break;
    case ULONG_TYPE:
      printf("ulong");
      break;
    case POINTER_TYPE:
      print_type(type->type_data.pointer_type.referenced_type);
      printf("*");
      break;
    case FUN_TYPE:
      printf("func(ret_type=");
      print_type(type->type_data.fun_type.return_type);
      printf("param_types=[");
      print_param_type_list(type->type_data.fun_type.param_types);
      printf("]");
      break;
  }
}

void print_expr(struct Expr* expr){
  switch (expr->type){
    case BINARY:
      print_bin_expr(&expr->expr.bin_expr);
      break;
    case UNARY:
      print_un_expr(&expr->expr.un_expr);
      break;
    case ASSIGN:
      print_assign_expr(&expr->expr.assign_expr);
      break;
    case POST_ASSIGN:
      print_post_assign_expr(&expr->expr.post_assign_expr);
      break;
    case CONDITIONAL:
      print_conditional_expr(&expr->expr.conditional_expr);
      break;
    case LIT:
      print_lit_expr(&expr->expr.lit_expr);
      break;
    case VAR:
      print_var_expr(&expr->expr.var_expr);
      break;
    case FUNCTION_CALL:
      print_fun_call_expr(&expr->expr.fun_call_expr);
      break;
    case CAST:
      print_cast_expr(&expr->expr.cast_expr);
      break;
    case ADDR_OF:
      print_addr_of_expr(&expr->expr.addr_of_expr);
      break;
    case DEREFERENCE:
      print_dereference_expr(&expr->expr.deref_expr);
      break;
  }
}

void print_bin_expr(struct BinaryExpr* bin_expr){
  switch (bin_expr->op){
    case ADD_OP:
      printf("AddOp(");
      break;
    case SUB_OP:
      printf("SubOp(");
      break;
    case MUL_OP:
      printf("MulOp(");
      break;
    case DIV_OP:
      printf("DivOp(");
      break;
    case MOD_OP:
      printf("ModOp(");
      break;
    case BIT_AND:
      printf("BitAnd(");
      break;
    case BIT_OR:
      printf("BitOr(");
      break;
    case BIT_XOR:
      printf("BitXor(");
      break;
    case BIT_SHL:
      printf("BitShl(");
      break;
    case BIT_SHR:
      printf("BitShr(");
      break;
    case BOOL_AND:
      printf("BoolAnd(");
      break;
    case BOOL_OR:
      printf("BoolOr(");
      break;
    case BOOL_EQ:
      printf("BoolEq(");
      break;
    case BOOL_NEQ:
      printf("BoolNeq(");
      break;
    case BOOL_LE:
      printf("BoolLe(");
      break;
    case BOOL_GE:
      printf("BoolGe(");
      break;
    case BOOL_LEQ:
      printf("BoolLeq(");
      break;
    case BOOL_GEQ:
      printf("BoolGeq(");
      break;
    case ASSIGN_OP:
      printf("AssignOp(");
      break;
    case PLUS_EQ_OP:
      printf("PlusEqOp(");
      break;
    case MINUS_EQ_OP:
      printf("MinusEqOp(");
      break;
    case MUL_EQ_OP:
      printf("MulEqOp(");
      break;
    case DIV_EQ_OP:
      printf("DivEqOp(");
      break;
    case MOD_EQ_OP:
      printf("ModEqOp(");
      break;
    case AND_EQ_OP:
      printf("AndEqOp(");
      break;
    case OR_EQ_OP:
      printf("OrEqOp(");
      break;
    case XOR_EQ_OP:
      printf("XorEqOp(");
      break;
    case SHL_EQ_OP:
      printf("ShlEqOp(");
      break;
    case SHR_EQ_OP:
      printf("ModOp(");
      break;
    case TERNARY_OP:
      printf("TernaryOp(");
      break;
  }
  print_expr(bin_expr->left);
  printf(", ");
  print_expr(bin_expr->right);
  printf(")");
}

void print_un_expr(struct UnaryExpr* un_expr){
  switch (un_expr->op){
    case COMPLEMENT:
      printf("Complement(");
      break;
    case NEGATE:
      printf("Negate(");
      break;
    case BOOL_NOT:
      printf("BoolNot(");
      break;
  }
  print_expr(un_expr->expr);
  printf(")");
}

void print_assign_expr(struct AssignExpr* assign_expr){
  printf("Assign(");
  print_expr(assign_expr->left);
  printf(", ");
  print_expr(assign_expr->right);
  printf(")");
}

void print_post_assign_expr(struct PostAssignExpr* post_expr){
  printf("PostAssign(");
  switch (post_expr->op){
    case POST_INC:
      printf("Inc");
      break;
    case POST_DEC:
      printf("Dec");
      break;
  }
  printf(", ");
  print_expr(post_expr->expr);
  printf(")");
}

void print_conditional_expr(struct ConditionalExpr* c_expr){
  printf("ConditionalExpr(");
  print_expr(c_expr->condition);
  printf(", ");
  print_expr(c_expr->left);
  printf(", ");
  print_expr(c_expr->right);
  printf(")");
}

void print_lit_expr(struct LitExpr* lit_expr){
  printf("LitExpr(");
  switch (lit_expr->type){
    case INT_CONST:
      printf("int, %d", lit_expr->value.int_val);
      break;
    case UINT_CONST:
      printf("uint, %u", lit_expr->value.uint_val);
      break;
    case LONG_CONST:
      printf("long, %ld", lit_expr->value.long_val);
      break;
    case ULONG_CONST:
      printf("ulong, %lu", lit_expr->value.ulong_val);
      break;
  }
  printf(")");
}

void print_var_expr(struct VarExpr* var_expr){
  printf("Var(");
  print_slice(var_expr->name);
  printf(")");
}

void print_args_list(struct ArgList* args_list){
  if (args_list == NULL) return;
  print_expr(&args_list->arg);
  printf(", ");
  print_args_list(args_list->next);
}

void print_fun_call_expr(struct FunctionCallExpr* fun_expr){
  printf("FunCallExpr(");
  print_slice(fun_expr->func_name);
  printf(", [");
  print_args_list(fun_expr->args);
  printf("])");
}

void print_cast_expr(struct CastExpr* cast_expr){
  printf("Cast(");
  print_type(cast_expr->target);
  printf(", ");
  print_expr(cast_expr->expr);
  printf(")");
}

void print_addr_of_expr(struct AddrOfExpr* expr){
  printf("AddrOf(");
  print_expr(expr->expr);
  printf(")");
}

void print_dereference_expr(struct DereferenceExpr* expr){
  printf("Dereference(");
  print_expr(expr->expr);
  printf(")");
}

void print_block_item(struct BlockItem* item, unsigned tabs){
  switch (item->type){
    case DCLR_ITEM:
      print_declaration(item->item.dclr, tabs);
      break;
    case STMT_ITEM:
      print_stmt(item->item.stmt, tabs);
  }
}

void print_block(struct Block* block, unsigned tabs){
  if (block == NULL) return;
  print_block_item(block->item, tabs);
  print_block(block->next, tabs);
}

void print_for_init(struct ForInit* for_init){
  switch (for_init->type){
    case DCLR_INIT:
      print_var_dclr(for_init->init.dclr_init);
    case EXPR_INIT:
      if (for_init->init.expr_init == NULL) return;
      print_expr(for_init->init.expr_init);
  }
}

void print_case_list(struct CaseList* case_list){
  if (case_list == NULL) return;
  switch (case_list->case_label.type){
    case INT_CASE:
      printf("IntCase %d", case_list->case_label.data);
    case DEFAULT_CASE:
      printf("DefaultCase");
  }
  printf(", ");
  print_case_list(case_list->next);
}

void print_return_stmt(struct ReturnStmt* ret_stmt, unsigned tabs){
  printf("Return(");
  print_expr(ret_stmt->expr);
  if (ret_stmt->func != NULL){
    printf(", ");
    print_slice(ret_stmt->func);
  }
  printf(");\n");
}

void print_expr_stmt(struct ExprStmt* expr_stmt, unsigned tabs){
  printf("ExprStmt("); print_expr(expr_stmt->expr); printf(");\n");
}

void print_if_stmt(struct IfStmt* if_stmt, unsigned tabs){
  printf("IfStmt(\n");
  print_tabs(tabs + 1); printf("c="); print_expr(if_stmt->condition); printf(", \n");
  print_tabs(tabs + 1); printf("left=\n");
  print_stmt(if_stmt->if_stmt, tabs + 2);
  if (if_stmt->else_stmt != NULL) {
    print_tabs(tabs + 1); printf("right=\n");
    print_stmt(if_stmt->else_stmt, tabs + 2);
  }
  print_tabs(tabs); printf(");\n");
}

void print_goto_stmt(struct GotoStmt* goto_stmt, unsigned tabs){
  printf("GotoStmt("); print_slice(goto_stmt->label); printf(");\n");
}

void print_labeled_stmt(struct LabeledStmt* labeled_stmt, unsigned tabs){
  printf("LabeledStmt(\n");
  print_tabs(tabs + 1); print_slice(labeled_stmt->label); printf(",\n");
  print_stmt(labeled_stmt->stmt, tabs + 1);
  print_tabs(tabs); printf(");\n"); 
}

void print_compound_stmt(struct CompoundStmt* compound_stmt, unsigned tabs){
  printf("CompoundStmt(\n");
  print_block(compound_stmt->block, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_break_stmt(struct BreakStmt* break_stmt, unsigned tabs){
  printf("BreakStmt(label=");
  if (break_stmt->label == NULL) printf("null");
  else print_slice(break_stmt->label);
  printf(");\n");
}

void print_continue_stmt(struct ContinueStmt* continue_stmt, unsigned tabs){
  printf("ContinueStmt(label=");
  if (continue_stmt->label == NULL) printf("null");
  else print_slice(continue_stmt->label);
  printf(");\n");
}

void print_while_stmt(struct WhileStmt* while_stmt, unsigned tabs){
  printf("WhileStmt(\n");
  if (while_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(while_stmt->label); printf(",\n");
  } 
  print_tabs(tabs + 1); print_expr(while_stmt->condition); printf(",\n");
  print_stmt(while_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_do_while_stmt(struct DoWhileStmt* do_while_stmt, unsigned tabs){
  printf("DoWhileStmt(\n");
  if (do_while_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(do_while_stmt->label); printf(",\n");
  } 
  print_stmt(do_while_stmt->statement, tabs + 1);
  print_tabs(tabs + 1); print_expr(do_while_stmt->condition); printf(",\n");
  print_tabs(tabs); printf(");\n");
}

void print_for_stmt(struct ForStmt* for_stmt, unsigned tabs){
  printf("ForStmt(\n");
  if (for_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(for_stmt->label); printf(",\n");
  }
  print_tabs(tabs + 1); print_for_init(for_stmt->init); printf(",\n");
  if (for_stmt->condition != NULL){
    print_tabs(tabs + 1); print_expr(for_stmt->condition); printf(",\n");
  }
  if (for_stmt->end != NULL){
    print_tabs(tabs + 1); print_expr(for_stmt->end); printf(",\n");
  }
  print_stmt(for_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_switch_stmt(struct SwitchStmt* switch_stmt, unsigned tabs){
  printf("SwitchStmt(\n");
  print_tabs(tabs + 1); printf("label="); 
  if (switch_stmt->label == NULL) printf("null"); 
  else print_slice(switch_stmt->label);
  printf("\n");
  if (switch_stmt->cases != NULL){
    print_tabs(tabs + 1); printf("cases=["); print_case_list(switch_stmt->cases); printf("]\n");
  }
  print_tabs(tabs + 1); print_expr(switch_stmt->condition); printf(",\n");
  print_stmt(switch_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_case_stmt(struct CaseStmt* case_stmt, unsigned tabs){
  printf("CaseStmt(\n"); 
  print_tabs(tabs + 1); print_expr(case_stmt->expr); printf(",\n");
  print_stmt(case_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_default_stmt(struct DefaultStmt* default_stmt, unsigned tabs){
  printf("DefaultStmt(\n");
  print_stmt(default_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_null_stmt(struct NullStmt* null_stmt, unsigned tabs){
  printf("NullStmt;\n");
}

void print_stmt(struct Statement* stmt, unsigned tabs){
  print_tabs(tabs);
  switch (stmt->type){
    case RETURN_STMT:
      print_return_stmt(&stmt->statement.ret_stmt, tabs);
      break;
    case EXPR_STMT:
      print_expr_stmt(&stmt->statement.expr_stmt, tabs); 
      break;
    case IF_STMT:
      print_if_stmt(&stmt->statement.if_stmt, tabs);
      break;
    case GOTO_STMT:
      print_goto_stmt(&stmt->statement.goto_stmt, tabs);
      break;
    case LABELED_STMT:
      print_labeled_stmt(&stmt->statement.labeled_stmt, tabs);
      break;
    case COMPOUND_STMT:
      print_compound_stmt(&stmt->statement.compound_stmt, tabs);
      break;
    case BREAK_STMT:
      print_break_stmt(&stmt->statement.break_stmt, tabs);
      break;
    case CONTINUE_STMT:
      print_continue_stmt(&stmt->statement.continue_stmt, tabs);
      break;
    case WHILE_STMT:
      print_while_stmt(&stmt->statement.while_stmt, tabs);
      break;
    case DO_WHILE_STMT:
      print_do_while_stmt(&stmt->statement.do_while_stmt, tabs);
      break;
    case FOR_STMT:
      print_for_stmt(&stmt->statement.for_stmt, tabs);
      break;
    case SWITCH_STMT:
      print_switch_stmt(&stmt->statement.switch_stmt, tabs);
      break;
    case CASE_STMT:
      print_case_stmt(&stmt->statement.case_stmt, tabs);
      break;
    case DEFAULT_STMT:
      print_default_stmt(&stmt->statement.default_stmt, tabs);
      break;
    case NULL_STMT:
      print_null_stmt(&stmt->statement.null_stmt, tabs);
      break;
  }
}

void print_var_dclr(struct VariableDclr* var_dclr){
  printf("VarDclr(");
  print_type(var_dclr->type); printf(", ");
  print_slice(var_dclr->name);
  if (var_dclr->init != NULL) {
    printf(", "); print_expr(var_dclr->init);
  }
  printf(")");
}

void print_declaration(struct Declaration* declaration, unsigned tabs){
  
}

/*-------------------------------------------------------------------------------------------------------*/

/* destructors */

void destroy_expr(struct Expr* expr){
  switch (expr->type){
    case BINARY:
      destroy_bin_expr(&expr->expr.bin_expr);
      break;
    case UNARY:
      destroy_un_expr(&expr->expr.un_expr);
      break;
    case ASSIGN:
      destroy_assign_expr(&expr->expr.assign_expr);
      break;
    case POST_ASSIGN:
      destroy_post_assign_expr(&expr->expr.post_assign_expr);
      break;
    case CONDITIONAL:
      destroy_conditional_expr(&expr->expr.conditional_expr);
      break;
    case LIT:
      destroy_lit_expr(&expr->expr.lit_expr);
      break;
    case VAR:
      destroy_var_expr(&expr->expr.var_expr);
      break;
    case FUNCTION_CALL:
      destroy_fun_call_expr(&expr->expr.fun_call_expr);
      break;
    case CAST:
      destroy_cast_expr(&expr->expr.cast_expr);
      break;
    case ADDR_OF:
      destroy_addr_of_expr(&expr->expr.addr_of_expr);
      break;
    case DEREFERENCE:
      destroy_dereference_expr(&expr->expr.deref_expr);
      break;
  }
  free(expr);
}

void destroy_bin_expr(struct BinaryExpr* expr){
  destroy_expr(expr->left);
  destroy_expr(expr->right);
}

void destroy_un_expr(struct UnaryExpr* expr){
  destroy_expr(expr->expr);
}

void destroy_assign_expr(struct AssignExpr* expr){
  destroy_expr(expr->left);
  destroy_expr(expr->right);
}

void destroy_post_assign_expr(struct PostAssignExpr* expr){
  destroy_expr(expr->expr);
}

void destroy_conditional_expr(struct ConditionalExpr* expr){
  destroy_expr(expr->condition);
  destroy_expr(expr->left);
  destroy_expr(expr->right);
}

void destroy_lit_expr(struct LitExpr* expr){}

void destroy_var_expr(struct VarExpr* expr){}

void destroy_arg_list(struct ArgList* args){
  if (args->next != NULL) destroy_arg_list(args->next);
  destroy_expr(&args->arg);
}

void destroy_fun_call_expr(struct FunctionCallExpr* expr){
  if (expr->args != NULL) destroy_arg_list(expr->args);
}

void destroy_cast_expr(struct CastExpr* expr){
  destroy_type(expr->target);
  destroy_expr(expr->expr);
}

void destroy_addr_of_expr(struct AddrOfExpr* expr){
  destroy_expr(expr->expr);
}

void destroy_dereference_expr(struct DereferenceExpr* expr){
  destroy_expr(expr->expr);
}

void destroy_param_list(struct ParamTypeList* params){
  if (params->next != NULL) free(params->next);
  free(params);
}

void destroy_type(struct Type* type){
  switch (type->type){
    case INT_TYPE:
    case LONG_TYPE:
    case UINT_TYPE:
    case ULONG_TYPE:
      free(type);
      break;
    case POINTER_TYPE:
      destroy_type(type->type_data.pointer_type.referenced_type);
      free(type);
      break;
    case FUN_TYPE:
      destroy_type(type->type_data.fun_type.return_type);
      destroy_param_list(type->type_data.fun_type.param_types);
      free(type);
      break;
  }
}

void destroy_type_spec_list(struct TypeSpecList* specs){
  if (specs->next != NULL) destroy_type_spec_list(specs->next);
  free(specs);
}

void destroy_abstract_declarator(struct AbstractDeclarator* declarator){
  if (declarator->type == ABSTRACT_BASE) free(declarator);
  else {
    destroy_abstract_declarator(declarator->data);
    free(declarator);
  }
}

void destroy_block_item(struct BlockItem* item){
  switch (item->type){
    case DCLR_ITEM:
      destroy_declaration(item->item.dclr);
      break;
    case STMT_ITEM:
      destroy_stmt(item->item.stmt);
      break;
  }
  free(item);
}

void destroy_block(struct Block* block){
  if (block == NULL) return;
  if (block->next != NULL) destroy_block(block->next);
  destroy_block_item(block->item);
  free(block);
}

void destroy_for_init(struct ForInit* for_init){
  switch(for_init->type){
    case DCLR_INIT:
      destroy_var_dclr(for_init->init.dclr_init);
      break;
    case EXPR_INIT:
      if (for_init->init.expr_init != NULL)
        destroy_expr(for_init->init.expr_init);
      break;
  }
  free(for_init);
}

void destroy_case_list(struct CaseList* case_list){
  if (case_list->next != NULL) destroy_case_list(case_list->next);
  free(case_list);
}

void destroy_return_stmt(struct ReturnStmt* ret_stmt){
  destroy_expr(ret_stmt->expr);
}

void destroy_expr_stmt(struct ExprStmt* expr_stmt){
  destroy_expr(expr_stmt->expr);
}

void destroy_if_stmt(struct IfStmt* if_stmt){
  destroy_expr(if_stmt->condition);
  destroy_stmt(if_stmt->if_stmt);
  if (if_stmt->else_stmt != NULL) destroy_stmt(if_stmt->else_stmt);
}

void destroy_goto_stmt(struct GotoStmt* goto_stmt){}

void destroy_labeled_stmt(struct LabeledStmt* labeled_stmt){
  destroy_stmt(labeled_stmt->stmt);
}

void destroy_compound_stmt(struct CompoundStmt* compound_stmt){
  destroy_block(compound_stmt->block);
}

void destroy_break_stmt(struct BreakStmt* break_stmt){}

void destroy_continue_stmt(struct ContinueStmt* continue_stmt){}

void destroy_while_stmt(struct WhileStmt* while_stmt){
  destroy_expr(while_stmt->condition);
  destroy_stmt(while_stmt->statement);
}

void destroy_do_while_stmt(struct DoWhileStmt* do_while_stmt){
  destroy_stmt(do_while_stmt->statement);
  destroy_expr(do_while_stmt->condition);
}

void destroy_for_stmt(struct ForStmt* for_stmt){
  if (for_stmt->init != NULL) destroy_for_init(for_stmt->init);
  if (for_stmt->condition != NULL) destroy_expr(for_stmt->condition);
  if (for_stmt->end != NULL) destroy_expr(for_stmt->end);
  destroy_stmt(for_stmt->statement);
}

void destroy_switch_stmt(struct SwitchStmt* switch_stmt){
  destroy_expr(switch_stmt->condition);
  destroy_stmt(switch_stmt->statement);
  if (switch_stmt->cases != NULL) destroy_case_list(switch_stmt->cases);
}

void destroy_case_stmt(struct CaseStmt* case_stmt){
  destroy_expr(case_stmt->expr);
  destroy_stmt(case_stmt->statement);
}

void destroy_default_stmt(struct DefaultStmt* default_stmt){
  destroy_stmt(default_stmt->statement);
}

void destroy_null_stmt(struct NullStmt* null_stmt){}

void destroy_stmt(struct Statement* stmt){
  switch (stmt->type){
    case RETURN_STMT:
      destroy_return_stmt(&stmt->statement.ret_stmt);
      break;
    case EXPR_STMT:
      destroy_expr_stmt(&stmt->statement.expr_stmt); 
      break;
    case IF_STMT:
      destroy_if_stmt(&stmt->statement.if_stmt);
      break;
    case GOTO_STMT:
      destroy_goto_stmt(&stmt->statement.goto_stmt);
      break;
    case LABELED_STMT:
      destroy_labeled_stmt(&stmt->statement.labeled_stmt);
      break;
    case COMPOUND_STMT:
      destroy_compound_stmt(&stmt->statement.compound_stmt);
      break;
    case BREAK_STMT:
      destroy_break_stmt(&stmt->statement.break_stmt);
      break;
    case CONTINUE_STMT:
      destroy_continue_stmt(&stmt->statement.continue_stmt);
      break;
    case WHILE_STMT:
      destroy_while_stmt(&stmt->statement.while_stmt);
      break;
    case DO_WHILE_STMT:
      destroy_do_while_stmt(&stmt->statement.do_while_stmt);
      break;
    case FOR_STMT:
      destroy_for_stmt(&stmt->statement.for_stmt);
      break;
    case SWITCH_STMT:
      destroy_switch_stmt(&stmt->statement.switch_stmt);
      break;
    case CASE_STMT:
      destroy_case_stmt(&stmt->statement.case_stmt);
      break;
    case DEFAULT_STMT:
      destroy_default_stmt(&stmt->statement.default_stmt);
      break;
    case NULL_STMT:
      destroy_null_stmt(&stmt->statement.null_stmt);
      break;
  }
  free(stmt);
}

void destroy_var_dclr(struct VariableDclr* var_dclr){
  if (var_dclr->init != NULL) destroy_expr(var_dclr->init);
  destroy_type(var_dclr->type);
  free(var_dclr);
}

void destroy_declarator(struct Declarator* declarator){
  
}

void destroy_declaration(struct Declaration* declaration){
  
}