#include <stdio.h>
#include <stdlib.h>

#include "AST.h"
#include "slice.h"

/* show instances */

void print_tabs(int tabs) {
  for (int i = 0; i < tabs; ++i) printf("    ");
}

void print_param_type_list(struct ParamTypeList* type_list){
  if (type_list == NULL) return;
  print_type(type_list->type);
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
    case SHORT_TYPE:
      printf("short");
      break;
    case USHORT_TYPE:
      printf("ushort");
      break;
    case CHAR_TYPE:
      printf("char");
      break;
    case SCHAR_TYPE:
      printf("schar");
      break;
    case UCHAR_TYPE:
      printf("uchar");
      break;
    case POINTER_TYPE:
      print_type(type->type_data.pointer_type.referenced_type);
      printf("*");
      break;
    case VOID_TYPE:
      printf("void");
      break;
    case FUN_TYPE:
      printf("func(ret_type=");
      print_type(type->type_data.fun_type.return_type);
      printf(", ");
      printf("param_types=[");
      print_param_type_list(type->type_data.fun_type.param_types);
      printf("]");
      printf(")");
      break;
    case ARRAY_TYPE:
      // Print array dimensions in declaration order (outermost to innermost).
      struct Type* element_type = type->type_data.array_type.element_type;
      while (element_type->type == ARRAY_TYPE) {
        element_type = element_type->type_data.array_type.element_type;
      }
      print_type(element_type);
      for (struct Type* cur = type; cur->type == ARRAY_TYPE;
           cur = cur->type_data.array_type.element_type) {
        printf("[%zu]", cur->type_data.array_type.size);
      }
      break;
    case STRUCT_TYPE:
      printf("struct ");
      print_slice_with_escapes(type->type_data.struct_type.name);
      break;
    case UNION_TYPE:
      printf("union ");
      print_slice_with_escapes(type->type_data.union_type.name);
      break;
    case ENUM_TYPE:
      printf("enum ");
      print_slice_with_escapes(type->type_data.enum_type.name);
      break;
    default:
      printf("unknown_type");
      break;
  }
}

static void print_storage_class(enum StorageClass storage){
  switch (storage){
    case NONE:
      printf("none");
      break;
    case STATIC:
      printf("static");
      break;
    case EXTERN:
      printf("extern");
      break;
  }
}

void print_expr(struct Expr* expr, int tabs){
  switch (expr->type){
    case BINARY:
      print_bin_expr(&expr->expr.bin_expr, tabs);
      break;
    case UNARY:
      print_un_expr(&expr->expr.un_expr, tabs);
      break;
    case ASSIGN:
      print_assign_expr(&expr->expr.assign_expr, tabs);
      break;
    case POST_ASSIGN:
      print_post_assign_expr(&expr->expr.post_assign_expr, tabs);
      break;
    case CONDITIONAL:
      print_conditional_expr(&expr->expr.conditional_expr, tabs);
      break;
    case LIT:
      print_lit_expr(&expr->expr.lit_expr);
      break;
    case VAR:
      print_var_expr(&expr->expr.var_expr);
      break;
    case FUNCTION_CALL:
      print_fun_call_expr(&expr->expr.fun_call_expr, tabs);
      break;
    case CAST:
      print_cast_expr(&expr->expr.cast_expr, tabs);
      break;
    case ADDR_OF:
      print_addr_of_expr(&expr->expr.addr_of_expr, tabs);
      break;
    case DEREFERENCE:
      print_dereference_expr(&expr->expr.deref_expr, tabs);
      break;
    case SUBSCRIPT:
      print_subscript_expr(&expr->expr.subscript_expr, tabs);
      break;
    case STRING:
      printf("String(");
      print_slice_with_escapes(expr->expr.string_expr.string);
      printf(")");
      break;
    case SIZEOF_EXPR:
      printf("SizeOf(");
      print_expr(expr->expr.sizeof_expr.expr, tabs);
      printf(")");
      break;
    case SIZEOF_T_EXPR:
      printf("SizeOfT(");
      print_type(expr->expr.sizeof_t_expr.type);
      printf(")");
      break;
    case STMT_EXPR:
      printf("\n");
      print_tabs(tabs + 1);
      printf("StmtExpr(\n");
      print_block(expr->expr.stmt_expr.block, tabs + 2);
      print_tabs(tabs + 1);
      printf(")\n");
      print_tabs(tabs);
      break;
    case DOT_EXPR:
      printf("DotExpr(");
      print_expr(expr->expr.dot_expr.struct_expr, tabs);
      printf(", ");
      print_slice_with_escapes(expr->expr.dot_expr.member);
      printf(")");
      break;
    case ARROW_EXPR:
      printf("ArrowExpr(");
      print_expr(expr->expr.arrow_expr.pointer_expr, tabs);
      printf(", ");
      print_slice_with_escapes(expr->expr.arrow_expr.member);
      printf(")");
      break;
    default:
      printf("unknown_expr %u\n", expr->type);
      exit(1);
      break;
  }
  if (expr->value_type == NULL){
    printf(":untyped");
  } else {
    printf(":");
    print_type(expr->value_type);
  }
}

void print_bin_expr(struct BinaryExpr* bin_expr, int tabs){
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
      printf("ShrEqOp(");
      break;
    case TERNARY_OP:
      printf("TernaryOp(");
      break;
    case COMMA_OP:
      printf("CommaOp(");
      break;
  }
  print_expr(bin_expr->left, tabs);
  printf(", ");
  print_expr(bin_expr->right, tabs);
  printf(")");
}

void print_un_expr(struct UnaryExpr* un_expr, int tabs){
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
    case UNARY_PLUS:
      printf("UnaryPlus(");
      break;
  }
  print_expr(un_expr->expr, tabs);
  printf(")");
}

void print_assign_expr(struct AssignExpr* assign_expr, int tabs){
  printf("Assign(");
  print_expr(assign_expr->left, tabs);
  printf(", ");
  print_expr(assign_expr->right, tabs);
  printf(")");
}

void print_post_assign_expr(struct PostAssignExpr* post_expr, int tabs){
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
  print_expr(post_expr->expr, tabs);
  printf(")");
}

void print_conditional_expr(struct ConditionalExpr* c_expr, int tabs){
  printf("ConditionalExpr(");
  print_expr(c_expr->condition, tabs);
  printf(", ");
  print_expr(c_expr->left, tabs);
  printf(", ");
  print_expr(c_expr->right, tabs);
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
    default:
      printf("unknown_const");
      break;
  }
  printf(")");
}

void print_var_expr(struct VarExpr* var_expr){
  printf("Var(");
  print_slice(var_expr->name);
  printf(")");
}

void print_args_list(struct ArgList* args_list, int tabs){
  if (args_list == NULL) return;
  print_expr(args_list->arg, tabs);
  printf(", ");
  print_args_list(args_list->next, tabs);
}

void print_fun_call_expr(struct FunctionCallExpr* fun_expr, int tabs){
  printf("FunCallExpr(");
  print_expr(fun_expr->func, tabs);
  printf(", [");
  print_args_list(fun_expr->args, tabs);
  printf("])");
}

void print_cast_expr(struct CastExpr* cast_expr, int tabs){
  printf("Cast(");
  print_type(cast_expr->target);
  printf(", ");
  print_expr(cast_expr->expr, tabs);
  printf(")");
}

void print_addr_of_expr(struct AddrOfExpr* expr, int tabs){
  printf("AddrOf(");
  print_expr(expr->expr, tabs);
  printf(")");
}

void print_dereference_expr(struct DereferenceExpr* expr, int tabs){
  printf("Dereference(");
  print_expr(expr->expr, tabs);
  printf(")");
}

void print_subscript_expr(struct SubscriptExpr* expr, int tabs){
  printf("Subscript(");
  print_expr(expr->array, tabs);
  printf(", ");
  print_expr(expr->index, tabs);
  printf(")");
}

void print_block_item(struct BlockItem* item, int tabs){
  switch (item->type){
    case DCLR_ITEM:
      print_declaration(item->item.dclr, tabs);
      break;
    case STMT_ITEM:
      print_stmt(item->item.stmt, tabs);
  }
}

void print_block(struct Block* block, int tabs){
  if (block == NULL) return;
  print_block_item(block->item, tabs);
  print_block(block->next, tabs);
}

void print_for_init(struct ForInit* for_init, int tabs){
  switch (for_init->type){
    case DCLR_INIT:
      print_var_dclr(for_init->init.dclr_init, tabs);
      break;
    case EXPR_INIT:
      if (for_init->init.expr_init == NULL) return;
      print_expr(for_init->init.expr_init, tabs);
  }
}

void print_case_list(struct CaseList* case_list, int tabs){
  if (case_list == NULL) return;
  switch (case_list->case_label.type){
    case INT_CASE:
      printf("IntCase %d", case_list->case_label.data);
      break;
    case DEFAULT_CASE:
      printf("DefaultCase");
      break;
  }
  printf(", ");
  print_case_list(case_list->next, tabs);
}

void print_return_stmt(struct ReturnStmt* ret_stmt, int tabs){
  printf("Return(");
  if (ret_stmt->expr == NULL){
    printf("void");
  } else {
    print_expr(ret_stmt->expr, tabs);
  }
  if (ret_stmt->func != NULL){
    printf(", ");
    print_slice(ret_stmt->func);
  }
  printf(");\n");
}

void print_expr_stmt(struct ExprStmt* expr_stmt, int tabs){
  printf("ExprStmt("); print_expr(expr_stmt->expr, tabs); printf(");\n");
}

void print_if_stmt(struct IfStmt* if_stmt, int tabs){
  printf("IfStmt(\n");
  print_tabs(tabs + 1); printf("c="); print_expr(if_stmt->condition, tabs); printf(", \n");
  print_tabs(tabs + 1); printf("left=\n");
  print_stmt(if_stmt->if_stmt, tabs + 2);
  if (if_stmt->else_stmt != NULL) {
    print_tabs(tabs + 1); printf("right=\n");
    print_stmt(if_stmt->else_stmt, tabs + 2);
  }
  print_tabs(tabs); printf(");\n");
}

void print_goto_stmt(struct GotoStmt* goto_stmt, int tabs){
  printf("GotoStmt("); print_slice(goto_stmt->label); printf(");\n");
}

void print_labeled_stmt(struct LabeledStmt* labeled_stmt, int tabs){
  printf("LabeledStmt(\n");
  print_tabs(tabs + 1); print_slice(labeled_stmt->label); printf(",\n");
  print_stmt(labeled_stmt->stmt, tabs + 1);
  print_tabs(tabs); printf(");\n"); 
}

void print_compound_stmt(struct CompoundStmt* compound_stmt, int tabs){
  printf("CompoundStmt(\n");
  print_block(compound_stmt->block, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_break_stmt(struct BreakStmt* break_stmt, int tabs){
  printf("BreakStmt(label=");
  if (break_stmt->label == NULL) printf("null");
  else print_slice(break_stmt->label);
  printf(");\n");
}

void print_continue_stmt(struct ContinueStmt* continue_stmt, int tabs){
  printf("ContinueStmt(label=");
  if (continue_stmt->label == NULL) printf("null");
  else print_slice(continue_stmt->label);
  printf(");\n");
}

void print_while_stmt(struct WhileStmt* while_stmt, int tabs){
  printf("WhileStmt(\n");
  if (while_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(while_stmt->label); printf(",\n");
  } 
  print_tabs(tabs + 1); print_expr(while_stmt->condition, tabs); printf(",\n");
  print_stmt(while_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_do_while_stmt(struct DoWhileStmt* do_while_stmt, int tabs){
  printf("DoWhileStmt(\n");
  if (do_while_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(do_while_stmt->label); printf(",\n");
  } 
  print_stmt(do_while_stmt->statement, tabs + 1);
  print_tabs(tabs + 1); print_expr(do_while_stmt->condition, tabs); printf(",\n");
  print_tabs(tabs); printf(");\n");
}

void print_for_stmt(struct ForStmt* for_stmt, int tabs){
  printf("ForStmt(\n");
  if (for_stmt->label != NULL){
    print_tabs(tabs + 1); print_slice(for_stmt->label); printf(",\n");
  }
  print_tabs(tabs + 1); print_for_init(for_stmt->init, tabs); printf(",\n");
  if (for_stmt->condition != NULL){
    print_tabs(tabs + 1); print_expr(for_stmt->condition, tabs); printf(",\n");
  }
  if (for_stmt->end != NULL){
    print_tabs(tabs + 1); print_expr(for_stmt->end, tabs); printf(",\n");
  }
  print_stmt(for_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_switch_stmt(struct SwitchStmt* switch_stmt, int tabs){
  printf("SwitchStmt(\n");
  print_tabs(tabs + 1); printf("label="); 
  if (switch_stmt->label == NULL) printf("null"); 
  else print_slice(switch_stmt->label);
  printf("\n");
  if (switch_stmt->cases != NULL){
    print_tabs(tabs + 1); printf("cases=["); print_case_list(switch_stmt->cases, tabs); printf("]\n");
  }
  print_tabs(tabs + 1); print_expr(switch_stmt->condition, tabs); printf(",\n");
  print_stmt(switch_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_case_stmt(struct CaseStmt* case_stmt, int tabs){
  printf("CaseStmt(\n"); 
  print_tabs(tabs + 1); print_expr(case_stmt->expr, tabs); printf(",\n");
  print_stmt(case_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_default_stmt(struct DefaultStmt* default_stmt, int tabs){
  printf("DefaultStmt(\n");
  print_stmt(default_stmt->statement, tabs + 1);
  print_tabs(tabs); printf(");\n");
}

void print_null_stmt(struct NullStmt* null_stmt, int tabs){
  printf("NullStmt;\n");
}

void print_stmt(struct Statement* stmt, int tabs){
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

void print_initializer(struct Initializer* init, int tabs){
  switch (init->init_type){
    case SINGLE_INIT:
      printf("SingleInit(");
      print_expr(init->init.single_init, tabs);
      printf(")");
      break;
    case COMPOUND_INIT:
      printf("CompoundInit(");
      struct InitializerList* cur = init->init.compound_init;
      while (cur != NULL){
        print_initializer(cur->init, tabs);
        if (cur->next != NULL) printf(", ");
        cur = cur->next;
      }
      printf(")");
      break;
  }
}

void print_var_attributes(struct VarAttributes attrs){
  if (attrs.cleanup_func != NULL){
    printf("cleanup = ");
    print_slice(attrs.cleanup_func);
    printf(", ");
  }
}

void print_var_dclr(struct VariableDclr* var_dclr, int tabs){
  printf("VarDclr(");
  print_storage_class(var_dclr->storage); printf(", ");
  print_type(var_dclr->type); printf(", ");
  print_var_attributes(var_dclr->attributes);
  print_slice(var_dclr->name);
  if (var_dclr->init != NULL) {
    printf(", "); print_initializer(var_dclr->init, tabs);
  }
  printf(")");
}

void print_param_list(struct ParamList* params, int tabs){
  if (params == NULL) return;
  print_var_dclr(&params->param, tabs);
  printf(", ");
  print_param_list(params->next, tabs);
}

void print_fun_dclr(struct FunctionDclr* fun_dclr, int tabs){
  printf("FunDclr(\n");
  print_tabs(tabs + 1); 
  print_slice(fun_dclr->name);
  printf(",\n");
  print_tabs(tabs + 1);
  printf("storage=");
  print_storage_class(fun_dclr->storage);
  printf("\n");
  print_tabs(tabs + 1);
  printf("params=[");
  print_param_list(fun_dclr->params, tabs + 1);
  printf("]\n");
  print_tabs(tabs + 1);
  printf("type=");
  if (fun_dclr->type == NULL){
    printf("untyped");
  } else {
    print_type(fun_dclr->type);
  }
  printf(",\n");
  if (fun_dclr->body != NULL){
    print_tabs(tabs + 1); printf("body=\n");
    print_block(fun_dclr->body, tabs + 2);
  } else {
    print_tabs(tabs + 1); printf("body=null\n");
  }
  print_tabs(tabs); printf(")");
}

void print_member_dclr(struct MemberDclr* member){
  printf("MemberDclr(name=");
  print_slice_with_escapes(member->name);
  printf(", type=");
  print_type(member->type);
  printf(")");
}

void print_struct_dclr(struct StructDclr* struct_dclr, int tabs){
  printf("\n");
  print_tabs(tabs);
  printf("StructDclr(name=");
  print_slice_with_escapes(struct_dclr->name);
  if (struct_dclr->members != NULL){
    printf(", members=[\n");
    struct MemberDclr* member = struct_dclr->members;
    while (member != NULL) {
      print_tabs(tabs + 1);
      print_member_dclr(member);
      printf(", \n");
      member = member->next;
    }
    print_tabs(tabs);
    printf("]");
  }
  printf(")");
}

void print_union_dclr(struct UnionDclr* union_dclr, int tabs){
  printf("\n");
  print_tabs(tabs);
  printf("UnionDclr(name=");
  print_slice_with_escapes(union_dclr->name);
  if (union_dclr->members != NULL){
    printf(", members=[\n");
    struct MemberDclr* member = union_dclr->members;
    while (member != NULL) {
      print_tabs(tabs + 1);
      print_member_dclr(member);
      printf(", \n");
      member = member->next;
    }
    print_tabs(tabs);
    printf("]");
  }
  printf(")");
}

void print_enum_dclr(struct EnumDclr* enum_dclr, int tabs){
  printf("\n");
  print_tabs(tabs);
  printf("EnumDclr(name=");
  print_slice_with_escapes(enum_dclr->name);
  if (enum_dclr->members != NULL){
    printf(", values=[\n");
    struct EnumMemberDclr* value = enum_dclr->members;
    while (value != NULL) {
      print_tabs(tabs + 1);
      printf("EnumValue(name=");
      print_slice_with_escapes(value->name);
      printf(", value=%d)", value->value);
      printf(", \n");
      value = value->next;
    }
    print_tabs(tabs);
    printf("]");
  }
  printf(")");
}

void print_declaration(struct Declaration* declaration, int tabs){
  print_tabs(tabs);
  switch (declaration->type){
    case VAR_DCLR:
      print_var_dclr(&declaration->dclr.var_dclr, tabs);
      printf(";\n");
      break;
    case FUN_DCLR:
      print_fun_dclr(&declaration->dclr.fun_dclr, tabs);
      printf(";\n");
      break;
    case STRUCT_DCLR:
      print_struct_dclr(&declaration->dclr.struct_dclr, tabs);
      printf(";\n");
      break;
    case UNION_DCLR:
      print_union_dclr(&declaration->dclr.union_dclr, tabs);
      printf(";\n");
      break;
    case ENUM_DCLR:
      print_enum_dclr(&declaration->dclr.enum_dclr, tabs);
      printf(";\n");
      break;
    case MEMBER_DCLR:
      print_member_dclr(&declaration->dclr.member_dclr);
      printf(";\n");
      break;
  }
}

void print_prog(struct Program* prog){
  printf("Program(\n");
  for (struct DeclarationList* cur = prog->dclrs; cur != NULL; cur = cur->next){
    print_declaration(&cur->dclr, 1);
  }
  printf(")\n");
}

bool compare_types(struct Type* a, struct Type* b) {
  if (a->type != b->type) {
    return false;
  }

  switch (a->type) {
    case SHORT_TYPE:
    case USHORT_TYPE:
    case INT_TYPE:
    case UINT_TYPE:
    case LONG_TYPE:
    case ULONG_TYPE:
    case CHAR_TYPE:
    case SCHAR_TYPE:
    case UCHAR_TYPE:
    case VOID_TYPE:
      return true; // primitive types match

    case POINTER_TYPE:
      return compare_types(a->type_data.pointer_type.referenced_type,
                           b->type_data.pointer_type.referenced_type);

    case ARRAY_TYPE: {
      struct ArrayType* arr_a = &a->type_data.array_type;
      struct ArrayType* arr_b = &b->type_data.array_type;
      if (arr_a->size != arr_b->size) {
        return false;
      }
      return compare_types(arr_a->element_type, arr_b->element_type);
    }

    case FUN_TYPE: {
      struct FunType* fun_a = &a->type_data.fun_type;
      struct FunType* fun_b = &b->type_data.fun_type;

      // compare return types
      if (!compare_types(fun_a->return_type, fun_b->return_type)) {
        return false;
      }

      // compare parameter types
      struct ParamTypeList* param_a = fun_a->param_types;
      struct ParamTypeList* param_b = fun_b->param_types;

      while (param_a != NULL && param_b != NULL) {
        if (!compare_types(param_a->type, param_b->type)) {
          return false;
        }
        param_a = param_a->next;
        param_b = param_b->next;
      }

      // both should reach the end
      return param_a == NULL && param_b == NULL;
    }
    case STRUCT_TYPE:
      return compare_slice_to_slice(a->type_data.struct_type.name,
                         b->type_data.struct_type.name);
    case UNION_TYPE:
      return compare_slice_to_slice(a->type_data.union_type.name,
                         b->type_data.union_type.name);
    case ENUM_TYPE:
      return compare_slice_to_slice(a->type_data.enum_type.name,
                         b->type_data.enum_type.name);
    default:
      printf("Type error: Unknown type in compare_types\n");
      return false; // unknown type
  }
}
