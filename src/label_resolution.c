#include "label_resolution.h"
#include "unique_name.h"
#include "label_map.h"
#include "arena.h"
#include "source_location.h"

#include <stdarg.h>
#include <stdio.h>

// Purpose: Resolve control-flow labels (loops/switches/gotos/cases) in the AST.
// Inputs: Traverses Program, Block, and Statement nodes produced by parsing.
// Outputs: Annotates statements with unique labels and case lists.
// Invariants/Assumptions: Labels are slices allocated from the arena.

// Purpose: Track the innermost loop/switch labels for break/continue/case.
// Inputs: Updated on entry/exit of loop or switch statements.
// Outputs: Referenced when labeling break/continue/case/default nodes.
// Invariants/Assumptions: Only one active loop and one active switch are tracked.
struct Slice* cur_loop_label = NULL;
struct Slice* cur_switch_label = NULL;
enum LabelType cur_label_type = -1;

// Purpose: Map user goto labels to unique labels within a function.
// Inputs: Populated during label_stmt traversal of labeled statements.
// Outputs: Used by resolve_gotos to rewrite goto targets.
// Invariants/Assumptions: Recreated per function and destroyed after labeling.
struct LabelMap* goto_labels = NULL;
// Purpose: Collects case/default labels for the current switch statement.
// Inputs: Appended during collect_cases traversal.
// Outputs: Stored on the switch statement node.
// Invariants/Assumptions: Reset when entering/leaving a switch statement.
struct CaseList* current_case_list = NULL;

// Purpose: Emit a formatted label-resolution error at a source location.
// Inputs: prefix identifies the pass; loc points into source text; fmt is printf-style.
// Outputs: Writes a message to stdout.
// Invariants/Assumptions: source_location_from_ptr handles NULL/unknown locations.
static void label_error_at(const char* prefix, const char* loc, const char* fmt, ...) {
  struct SourceLocation where = source_location_from_ptr(loc);
  const char* filename = source_filename_for_ptr(loc);
  if (where.line == 0) {
    printf("%s: ", prefix);
  } else {
    printf("%s at %s:%zu:%zu: ", prefix, filename, where.line, where.column);
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}

// Purpose: Label all functions in a program and resolve gotos/cases.
// Inputs: prog is the AST for the full translation unit.
// Outputs: Returns true on success; false on any labeling error.
// Invariants/Assumptions: Each function body is labeled independently.
bool label_loops(struct Program* prog) {
  for (struct DeclarationList* decl = prog->dclrs; decl != NULL; decl = decl->next) {
    // only need to label functions, not global variables
    if (decl->dclr.type == FUN_DCLR) {
      struct FunctionDclr* func_dclr = &decl->dclr.dclr.fun_dclr;
      // only label if there is a body
      if (func_dclr->body != NULL) {
        // Each function gets its own goto-label map and labeling pass.
        goto_labels = create_label_map(256);

        if (!label_block(func_dclr->name, func_dclr->body)) {
          return false;
        }

        if (!resolve_gotos(func_dclr->body)) {
          return false;
        }

        if (!collect_cases(func_dclr->body)) {
          return false;
        }

        destroy_label_map(goto_labels);
      }
    }
  }

  return true;
}

// -------------------------------- loop/switch labeling -------------------------------- //

// Purpose: Apply label assignment to a statement subtree.
// Inputs: func_name is the enclosing function name; stmt is the node to label.
// Outputs: Returns true on success; false on any invalid label usage.
// Invariants/Assumptions: Uses global label state to handle nesting.
bool label_stmt(struct Slice* func_name, struct Statement* stmt) {
  switch (stmt->type) {
    case WHILE_STMT: {
      struct WhileStmt* while_stmt = &stmt->statement.while_stmt;
      // generate label
      struct Slice* label = make_unique_label(func_name, "while");

      // save previous label state
      enum LabelType prev_label_type = cur_label_type;
      struct Slice* prev_loop_label = cur_loop_label;

      // Entered a loop: update current label state for break/continue.
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

      // Entered a loop: update current label state for break/continue.
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

      // Entered a loop: update current label state for break/continue.
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

      // Entered a switch: update current label state for break/case/default.
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
        label_error_at("Loop Labeling Error", stmt->loc,
                       "break statement outside loop/switch");
        return false;
      }

      // assign current label to break statement
      break_stmt->label = cur_label_type == LOOP ? cur_loop_label : cur_switch_label;
      break;
    }
    case CONTINUE_STMT: {
      struct ContinueStmt* continue_stmt = &stmt->statement.continue_stmt;
      if (cur_loop_label == NULL) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "continue statement outside loop");
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
      if (label_map_contains(goto_labels, stmt->statement.labeled_stmt.label)) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "multiple definitions for goto label %.*s",
                       (int)stmt->statement.labeled_stmt.label->len,
                       stmt->statement.labeled_stmt.label->start);
        return false;
      }

      // Map user label -> unique label to avoid collisions across scopes.
      struct Slice* unique_label = make_unique_label(func_name, "goto");
      label_map_insert(goto_labels, stmt->statement.labeled_stmt.label, unique_label);

      stmt->statement.labeled_stmt.label = unique_label;

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
        label_error_at("Loop Labeling Error", stmt->loc,
                       "case statement outside switch");
        return false;
      }

      if (case_stmt->expr->type != LIT) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "case statement with non-constant expression");
        return false;
      }

      // append ".case.num" to current switch label
      int num = case_stmt->expr->expr.lit_expr.value.int_val;
      case_stmt->label = make_case_label(cur_switch_label, num);

      if (!label_stmt(func_name, case_stmt->statement)) {
        return false;
      }
      break;
    }
    case DEFAULT_STMT: {
      struct DefaultStmt* default_stmt = &stmt->statement.default_stmt;
      if (cur_switch_label == NULL) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "default statement outside switch");
        return false;
      }

      // assign current switch label to default statement
      default_stmt->label = slice_concat(cur_switch_label, ".default");

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
    

// Purpose: Label each statement item inside a block.
// Inputs: func_name is the enclosing function name; block is the block list.
// Outputs: Returns true on success; false on any labeling error.
// Invariants/Assumptions: Declaration items do not require labeling.
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
        label_error_at("Loop Labeling Error", NULL,
                       "unknown block item type");
        return false;
    }
  }
  return true;
}

// Purpose: Resolve nested statements while rewriting goto targets.
// Inputs: stmt is the statement subtree to traverse.
// Outputs: Returns true on success; false if a goto target is missing.
// Invariants/Assumptions: goto_labels is populated for this function.
static bool resolve_stmt(struct Statement* stmt) {
  if (stmt->type == LABELED_STMT) {
    struct LabeledStmt* labeled_stmt = &stmt->statement.labeled_stmt;
    if (!resolve_stmt(labeled_stmt->stmt)) {
      return false;
    }
  } else if (stmt->type == COMPOUND_STMT) {
    struct Block* inner_block = stmt->statement.compound_stmt.block;
    if (!resolve_gotos(inner_block)) {
      return false;
    }
  } else if (stmt->type == IF_STMT) {
    struct IfStmt* if_stmt = &stmt->statement.if_stmt;
    if (!resolve_stmt(if_stmt->if_stmt)) {
      return false;
    }
    if (if_stmt->else_stmt != NULL) {
      if (!resolve_stmt(if_stmt->else_stmt)) {
        return false;
      }
    }
  } else if (stmt->type == WHILE_STMT) {
    struct WhileStmt* while_stmt = &stmt->statement.while_stmt;
    if (!resolve_stmt(while_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == DO_WHILE_STMT) {
    struct DoWhileStmt* do_while_stmt = &stmt->statement.do_while_stmt;
    if (!resolve_stmt(do_while_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == FOR_STMT) {
    struct ForStmt* for_stmt = &stmt->statement.for_stmt;
    if (!resolve_stmt(for_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == SWITCH_STMT) {
    struct SwitchStmt* switch_stmt = &stmt->statement.switch_stmt;
    if (!resolve_stmt(switch_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == CASE_STMT) {
    struct CaseStmt* case_stmt = &stmt->statement.case_stmt;
    if (!resolve_stmt(case_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == DEFAULT_STMT) {
    struct DefaultStmt* default_stmt = &stmt->statement.default_stmt;
    if (!resolve_stmt(default_stmt->statement)) {
      return false;
    }
  } else if (stmt->type == GOTO_STMT) {
    struct GotoStmt* goto_stmt = &stmt->statement.goto_stmt;
    // Replace user label with the unique label assigned during labeling.
    struct Slice* target_label = label_map_get(goto_labels, goto_stmt->label);
    if (target_label == NULL) {
      label_error_at("Goto Resolution Error", stmt->loc,
                     "label %.*s has no definition",
                     (int)goto_stmt->label->len, goto_stmt->label->start);
      return false;
    }
    goto_stmt->label = target_label;
  }

  return true;
}

// Purpose: Resolve goto statements within a block by replacing their labels.
// Inputs: block is the block list containing statements.
// Outputs: Returns true on success; false on unresolved labels.
// Invariants/Assumptions: Labeling pass created the goto label map.
bool resolve_gotos(struct Block* block) {
  for (struct Block* item = block; item != NULL; item = item->next) {
    // only need to resolve statements
    if (item->item->type == STMT_ITEM) {
      if (!resolve_stmt(item->item->item.stmt)) { 
        return false;
      }
    }
  }
  return true;
}

// -------------------------------- case collection -------------------------------- //

// Purpose: Traverse statements to collect case/default labels per switch.
// Inputs: stmt is the statement subtree to inspect.
// Outputs: Returns true on success; false on duplicate/invalid cases.
// Invariants/Assumptions: current_case_list tracks the active switch.
bool collect_cases_stmt(struct Statement* stmt){
  switch (stmt->type) {
    case WHILE_STMT: {
      struct WhileStmt* while_stmt = &stmt->statement.while_stmt;
      return collect_cases_stmt(while_stmt->statement);
    }
    case DO_WHILE_STMT: {
      struct DoWhileStmt* do_while_stmt = &stmt->statement.do_while_stmt;
      return collect_cases_stmt(do_while_stmt->statement);
    }
    case FOR_STMT: {
      struct ForStmt* for_stmt = &stmt->statement.for_stmt;
      return collect_cases_stmt(for_stmt->statement);
    }
    case IF_STMT: {
      struct IfStmt* if_stmt = &stmt->statement.if_stmt;
      if (!collect_cases_stmt(if_stmt->if_stmt)) {
        return false;
      }
      if (if_stmt->else_stmt != NULL) {
        if (!collect_cases_stmt(if_stmt->else_stmt)) {
          return false;
        }
      }
      return true;
    }
    case COMPOUND_STMT: {
      struct Block* block = stmt->statement.compound_stmt.block;
      return collect_cases(block);
    }
    case LABELED_STMT: {
      struct LabeledStmt* labeled_stmt = &stmt->statement.labeled_stmt;
      return collect_cases_stmt(labeled_stmt->stmt);
    }
    case CASE_STMT: {
      // extract case value
      struct CaseStmt* case_stmt = &stmt->statement.case_stmt;
      if (case_stmt->expr->type != LIT) {
        label_error_at("Case Collection Error", stmt->loc,
                       "case statement with non-constant expression");
        return false;
      }
      struct LitExpr* lit_expr = &case_stmt->expr->expr.lit_expr;
      int case_value = lit_expr->value.int_val;

      // Check for duplicate case values within the current switch.
      struct CaseList* case_iter = current_case_list;
      while (case_iter != NULL) {
        if (case_iter->case_label.type == INT_CASE && case_iter->case_label.data == case_value) {
          label_error_at("Case Collection Error", stmt->loc,
                         "duplicate case %d", case_value);
          return false;
        }
        case_iter = case_iter->next;
      }

      // add case to current case list
      struct CaseList* new_case = arena_alloc(sizeof(struct CaseList));
      new_case->case_label.type = INT_CASE;
      new_case->case_label.data = case_value;
      new_case->next = current_case_list;
      current_case_list = new_case;

      return collect_cases_stmt(case_stmt->statement);
    }
    case DEFAULT_STMT: {
      // Ensure only one default per switch.
      struct CaseList* case_iter = current_case_list;
      while (case_iter != NULL) {
        if (case_iter->case_label.type == DEFAULT_CASE) {
          label_error_at("Case Collection Error", stmt->loc,
                         "duplicate default case");
          return false;
        }
        case_iter = case_iter->next;
      }
      struct DefaultStmt* default_stmt = &stmt->statement.default_stmt;
      // add default case to current case list
      struct CaseList* new_case = arena_alloc(sizeof(struct CaseList));
      new_case->case_label.type = DEFAULT_CASE;
      new_case->next = current_case_list;
      current_case_list = new_case;
      return collect_cases_stmt(default_stmt->statement);
    }
    case SWITCH_STMT: {
      // Each switch statement collects its own case list.
      struct CaseList* prev_case_list = current_case_list;
      current_case_list = NULL;

      if (!collect_cases_stmt(stmt->statement.switch_stmt.statement)) {
        return false;
      }

      // assign collected cases to switch statement
      stmt->statement.switch_stmt.cases = current_case_list;

      current_case_list = prev_case_list;
      return true;
    }
    default:
      // no cases to collect
      return true;
  }
}

// Purpose: Collect case/default labels for every switch in a block.
// Inputs: block is the block list containing statements.
// Outputs: Returns true on success; false on case collection errors.
// Invariants/Assumptions: Case expressions must be literal integers.
bool collect_cases(struct Block* block) {
  for (struct Block* item = block; item != NULL; item = item->next) {
    // only need to process statements
    if (item->item->type == STMT_ITEM) {
      if (!collect_cases_stmt(item->item->item.stmt)) {
        return false;
      }
    }
  }
  return true;
}

// Purpose: Synthesize a unique label for a switch case value.
// Inputs: switch_label is the base switch label; case_value is the integer literal.
// Outputs: Returns a new Slice containing "switch.case.N".
// Invariants/Assumptions: Uses arena allocation for the label buffer.
struct Slice* make_case_label(struct Slice* switch_label, int case_value) {
  // append ".case.num" to current switch label
  unsigned id_len = counter_len(case_value);
  char* case_label_str = (char*)arena_alloc(switch_label->len + 6 + id_len); // len(".case.") == 6
  for (size_t i = 0; i < switch_label->len; i++) {
    case_label_str[i] = switch_label->start[i];
  }
  case_label_str[switch_label->len] = '.';
  case_label_str[switch_label->len + 1] = 'c';
  case_label_str[switch_label->len + 2] = 'a';
  case_label_str[switch_label->len + 3] = 's';
  case_label_str[switch_label->len + 4] = 'e';
  case_label_str[switch_label->len + 5] = '.';

  for (unsigned i = 0; i < id_len; i++) {
    case_label_str[switch_label->len + 6 + id_len - 1 - i] = '0' + (case_value % 10);
    case_value /= 10;
  }

  struct Slice* case_label = (struct Slice*)arena_alloc(sizeof(struct Slice));
  case_label->start = case_label_str;
  case_label->len = switch_label->len + 6 + id_len;

  return case_label;
}
