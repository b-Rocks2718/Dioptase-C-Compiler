#include "loop_resolution.h"
#include "unique_name.h"

#include <stdio.h>

struct Slice* cur_loop_label = NULL;
struct Slice* cur_switch_label = NULL;
enum LabelType cur_label_type = -1;

bool label_loops(struct Program* prog) {
  for (struct DeclarationList* decl = prog->dclrs; decl != NULL; decl = decl->next) {
    // only need to label functions, not global variables
    if (decl->dclr.type == FUN_DCLR) {
      struct FunctionDclr* func_dclr = &decl->dclr.dclr.fun_dclr;
      // only label if there is a body
      if (func_dclr->body != NULL) {
        if (!label_block(func_dclr->name, func_dclr->body)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool label_stmt(struct Slice* func_name, struct Statement* stmt) {
  switch (stmt->type) {
    case WHILE_STMT: {
      struct WhileStmt* while_stmt = &stmt->statement.while_stmt;
      // generate label
      struct Slice* label = make_unique_label(func_name, "while");

      // save previous label state
      enum LabelType prev_label_type = cur_label_type;
      struct Slice* prev_loop_label = cur_loop_label;

      // entered a loop, update current label state
      cur_loop_label = label;
      cur_label_type = LOOP;

      // label body
      if (!label_stmt(func_name, while_stmt->statement)) {
        return false;
      }

      // assign label
      while_stmt->label = label;

      // restore previous label state
      cur_loop_label = prev_loop_label;
      cur_label_type = prev_label_type;

      break;
    }
    case DO_WHILE_STMT: {
      struct DoWhileStmt* do_while_stmt = &stmt->statement.do_while_stmt;
      // generate label
      struct Slice* label = make_unique_label(func_name, "do_while");

      // save previous label state
      enum LabelType prev_label_type = cur_label_type;
      struct Slice* prev_loop_label = cur_loop_label;

      // entered a loop, update current label state
      cur_loop_label = label;
      cur_label_type = LOOP;

      // label body
      if (!label_stmt(func_name, do_while_stmt->statement)) {
        return false;
      }
      // assign label
      do_while_stmt->label = label;

      // restore previous label state
      cur_loop_label = prev_loop_label;
      cur_label_type = prev_label_type;

      break;
    }
    case FOR_STMT: {
      struct ForStmt* for_stmt = &stmt->statement.for_stmt;
      // generate label
      struct Slice* label = make_unique_label(func_name, "for");

      // save previous label state
      enum LabelType prev_label_type = cur_label_type;
      struct Slice* prev_loop_label = cur_loop_label;

      // entered a loop, update current label state
      cur_loop_label = label;
      cur_label_type = LOOP;

      // label body
      if (!label_stmt(func_name, for_stmt->statement)) {
        return false;
      }
      // assign label
      for_stmt->label = label;

      // restore previous label state
      cur_loop_label = prev_loop_label;
      cur_label_type = prev_label_type;

      break;
    }
    case SWITCH_STMT: {
      struct SwitchStmt* switch_stmt = &stmt->statement.switch_stmt;
      // generate label
      struct Slice* label = make_unique_label(func_name, "switch");

      // save previous label state
      enum LabelType prev_label_type = cur_label_type;
      struct Slice* prev_switch_label = cur_switch_label;

      // entered a switch, update current label state
      cur_switch_label = label;
      cur_label_type = SWITCH;

      // label body
      if (!label_stmt(func_name, switch_stmt->statement)) {
        return false;
      }
      // assign label
      switch_stmt->label = label;

      // restore previous label state
      cur_switch_label = prev_switch_label;
      cur_label_type = prev_label_type;

      break;
    }
    case BREAK_STMT: {
      struct BreakStmt* break_stmt = &stmt->statement.break_stmt;

      if (cur_label_type == -1) {
        printf("Loop Labeling Error: Break statement outside loop/switch\n");
        return false;
      }

      // assign current label to break statement
      break_stmt->label = cur_label_type == LOOP ? cur_loop_label : cur_switch_label;
      break;
    }
    case CONTINUE_STMT: {
      struct ContinueStmt* continue_stmt = &stmt->statement.continue_stmt;
      if (cur_loop_label == NULL) {
        printf("Loop Labeling Error: Continue statement outside loop\n");
        return false;
      }
      // assign current label to continue statement
      continue_stmt->label = cur_loop_label;
      break;
    }
    case COMPOUND_STMT: {
      struct Block* block = stmt->statement.compound_stmt.block;
      if (!label_block(func_name, block)) {
        return false;
      }
      break;
    }
    case IF_STMT: {
      struct IfStmt* if_stmt = &stmt->statement.if_stmt;
      if (!label_stmt(func_name, if_stmt->if_stmt)) {
        return false;
      }
      if (if_stmt->else_stmt != NULL) {
        if (!label_stmt(func_name, if_stmt->else_stmt)) {
          return false;
        }
      }
      break;
    }
    case LABELED_STMT: {
      struct LabeledStmt* labeled_stmt = &stmt->statement.labeled_stmt;
      if (!label_stmt(func_name, labeled_stmt->stmt)) {
        return false;
      }
      break;
    }
    case RETURN_STMT: {
      struct ReturnStmt* ret_stmt = &stmt->statement.ret_stmt;
      // assign function name as return label
      ret_stmt->func = func_name;
      break;
    }
    case CASE_STMT: {
      struct CaseStmt* case_stmt = &stmt->statement.case_stmt;

      if (cur_switch_label == NULL) {
        printf("Loop Labeling Error: Case statement outside switch\n");
        return false;
      }

      if (case_stmt->expr->type != LIT) {
        printf("Loop Labeling Error: Case statement with non-constant expression\n");
        return false;
      }

      // assign current switch label to case statement
      case_stmt->label = cur_switch_label;

      if (!label_stmt(func_name, case_stmt->statement)) {
        return false;
      }
      break;
    }
    case DEFAULT_STMT: {
      struct DefaultStmt* default_stmt = &stmt->statement.default_stmt;
      if (cur_switch_label == NULL) {
        printf("Loop Labeling Error: Default statement outside switch\n");
        return false;
      }

      // assign current switch label to default statement
      default_stmt->label = cur_switch_label;

      if (!label_stmt(func_name, default_stmt->statement)) {
        return false;
      }
      break;
    }
    default:
      // no labeling needed
      break;
  }

  return true;
}
    

bool label_block(struct Slice* func_name, struct Block* block) {
  for (struct Block* item = block; item != NULL; item = item->next) {
    switch (item->item->type) {
      case STMT_ITEM:
        if (!label_stmt(func_name, item->item->item.stmt)) {
          return false;
        }
        break;
      case DCLR_ITEM:
        // no need to label declarations
        break;
      default:
        printf("Loop Labeling Error: Unknown block item type\n");
        return false;
    }
  }
  return true;
}
