#include "label_resolution.h"
#include "unique_name.h"
#include "label_map.h"
#include "arena.h"
#include "source_location.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

// Resolve control-flow labels (loops/switches/gotos/cases) in the AST.
// Annotates statements with unique labels and case lists.

// Track the innermost loop/switch labels for break/continue/case.
// Referenced when labeling break/continue/case/default nodes.
struct Slice* cur_loop_label = NULL;
struct Slice* cur_switch_label = NULL;
enum LabelType cur_label_type = -1;

// Map user goto labels to unique labels within a function.
// Used by resolve_gotos to rewrite goto targets.
struct LabelMap* goto_labels = NULL;
// Collects case/default labels for the current switch statement.
// Stored on the switch statement node.
// Reset when entering/leaving a switch statement.
struct CaseList* current_case_list = NULL;

// Emit a formatted label-resolution error at a source location.
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

// Compute the decimal digit length of a 32-bit unsigned value.
// Returns the number of base-10 digits needed.
// Always returns at least 1.
static unsigned u32_len(uint32_t value) {
  unsigned len = 0;
  do {
    len++;
    value /= 10u;
  } while (value != 0u);
  return len;
}

// Label all functions in a program and resolve gotos/cases.
// Returns true on success; false on any labeling error.
bool label_loops(struct Program* prog) {
  for (struct DeclarationList* decl = prog->dclrs; decl != NULL; decl = decl->next) {
    // only need to label functions, not global variables
    if (decl->dclr.type == FUN_DCLR) {
      struct FunctionDclr* func_dclr = &decl->dclr.dclr.fun_dclr;
      // only label if there is a body
      if (func_dclr->body != NULL) {
        // Each function gets its own goto-label map and labeling pass.
        goto_labels = create_label_map(256);
        bool labeled = label_block(func_dclr->name, func_dclr->body) &&
                       resolve_gotos(func_dclr->body) &&
                       collect_cases(func_dclr->body);
        destroy_label_map(goto_labels);
        goto_labels = NULL;
        if (!labeled) {
          return false;
        }
      }
    }
  }

  return true;
}

// -------------------------------- loop/switch labeling -------------------------------- //

// The innermost loop/switch labels that break, continue, case, and default
// refer to; saved on entry to a loop or switch and restored on exit.
struct LabelState {
  struct Slice* loop_label;
  struct Slice* switch_label;
  enum LabelType label_type;
};

// Capture the current labeling state before entering a loop or switch.
static struct LabelState save_label_state(void) {
  struct LabelState state = { cur_loop_label, cur_switch_label, cur_label_type };
  return state;
}

// Restore the labeling state captured when the loop or switch was entered.
static void restore_label_state(struct LabelState state) {
  cur_loop_label = state.loop_label;
  cur_switch_label = state.switch_label;
  cur_label_type = state.label_type;
}

// Assign loop, switch, case, default, break, continue, goto-target, and return
// labels. Loops and switches walk their own children so that unique-label
// numbering follows the established order (a for loop's body is labeled before
// its init, condition, and step).
static enum AstWalkAction label_visit_stmt(struct AstVisitor* v, struct Statement* stmt) {
  struct Slice* func_name = v->ctx;
  union StatementVariant* s = &stmt->statement;
  switch (stmt->type) {
    case WHILE_STMT: {
      if (!ast_walk_expr(v, s->while_stmt.condition)) {
        return AST_WALK_STOP;
      }
      struct Slice* label = make_unique_label(func_name, "while");
      struct LabelState saved = save_label_state();
      cur_loop_label = label;
      cur_label_type = LOOP;
      if (!ast_walk_stmt(v, s->while_stmt.statement)) {
        return AST_WALK_STOP;
      }
      s->while_stmt.label = label;
      restore_label_state(saved);
      return AST_WALK_SKIP;
    }
    case DO_WHILE_STMT: {
      struct Slice* label = make_unique_label(func_name, "do_while");
      struct LabelState saved = save_label_state();
      cur_loop_label = label;
      cur_label_type = LOOP;
      if (!ast_walk_stmt(v, s->do_while_stmt.statement) ||
          !ast_walk_expr(v, s->do_while_stmt.condition)) {
        return AST_WALK_STOP;
      }
      s->do_while_stmt.label = label;
      restore_label_state(saved);
      return AST_WALK_SKIP;
    }
    case FOR_STMT: {
      struct Slice* label = make_unique_label(func_name, "for");
      struct LabelState saved = save_label_state();
      cur_loop_label = label;
      cur_label_type = LOOP;
      if (!ast_walk_stmt(v, s->for_stmt.statement) ||
          !ast_walk_for_init(v, s->for_stmt.init) ||
          !ast_walk_expr(v, s->for_stmt.condition) ||
          !ast_walk_expr(v, s->for_stmt.end)) {
        return AST_WALK_STOP;
      }
      s->for_stmt.label = label;
      restore_label_state(saved);
      return AST_WALK_SKIP;
    }
    case SWITCH_STMT: {
      struct Slice* label = make_unique_label(func_name, "switch");
      struct LabelState saved = save_label_state();
      cur_switch_label = label;
      cur_label_type = SWITCH;
      if (!ast_walk_expr(v, s->switch_stmt.condition) ||
          !ast_walk_stmt(v, s->switch_stmt.statement)) {
        return AST_WALK_STOP;
      }
      s->switch_stmt.label = label;
      restore_label_state(saved);
      return AST_WALK_SKIP;
    }
    case BREAK_STMT:
      if (cur_label_type == -1) {
        label_error_at("Loop Labeling Error", stmt->loc, "break statement outside loop/switch");
        return AST_WALK_STOP;
      }
      s->break_stmt.label = cur_label_type == LOOP ? cur_loop_label : cur_switch_label;
      return AST_WALK_CHILDREN;
    case CONTINUE_STMT:
      if (cur_loop_label == NULL) {
        label_error_at("Loop Labeling Error", stmt->loc, "continue statement outside loop");
        return AST_WALK_STOP;
      }
      s->continue_stmt.label = cur_loop_label;
      return AST_WALK_CHILDREN;
    case LABELED_STMT: {
      struct Slice* user_label = s->labeled_stmt.label;
      if (label_map_contains(goto_labels, user_label)) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "multiple definitions for goto label %.*s",
                       (int)user_label->len, user_label->start);
        return AST_WALK_STOP;
      }
      // Map user label -> unique label to avoid collisions across scopes.
      struct Slice* unique_label = make_unique_label(func_name, "goto");
      label_map_insert(goto_labels, user_label, unique_label);
      s->labeled_stmt.label = unique_label;
      return AST_WALK_CHILDREN;
    }
    case RETURN_STMT:
      s->ret_stmt.func = func_name;
      return AST_WALK_CHILDREN;
    case CASE_STMT:
      if (cur_switch_label == NULL) {
        label_error_at("Loop Labeling Error", stmt->loc, "case statement outside switch");
        return AST_WALK_STOP;
      }
      if (s->case_stmt.expr->type != LIT) {
        label_error_at("Loop Labeling Error", stmt->loc,
                       "case statement with non-constant expression");
        return AST_WALK_STOP;
      }
      s->case_stmt.label = make_case_label(cur_switch_label,
                                           s->case_stmt.expr->expr.lit_expr.value.int_val);
      return AST_WALK_CHILDREN;
    case DEFAULT_STMT:
      if (cur_switch_label == NULL) {
        label_error_at("Loop Labeling Error", stmt->loc, "default statement outside switch");
        return AST_WALK_STOP;
      }
      s->default_stmt.label = slice_concat(cur_switch_label, ".default");
      return AST_WALK_CHILDREN;
    default:
      return AST_WALK_CHILDREN;
  }
}

// Label each statement inside a block.
// Returns true on success; false on any labeling error.
bool label_block(struct Slice* func_name, struct Block* block) {
  struct AstVisitor visitor = { label_visit_stmt, NULL, func_name };
  return ast_walk_block(&visitor, block);
}

// Label a single statement subtree.
// Returns true on success; false on any labeling error.
bool label_stmt(struct Slice* func_name, struct Statement* stmt) {
  struct AstVisitor visitor = { label_visit_stmt, NULL, func_name };
  return ast_walk_stmt(&visitor, stmt);
}

// -------------------------------- goto resolution -------------------------------- //

// Rewrite each goto's target to the unique label its definition received.
static enum AstWalkAction resolve_goto_visit_stmt(struct AstVisitor* v, struct Statement* stmt) {
  (void)v;
  if (stmt->type != GOTO_STMT) {
    return AST_WALK_CHILDREN;
  }
  struct GotoStmt* goto_stmt = &stmt->statement.goto_stmt;
  struct Slice* target_label = label_map_get(goto_labels, goto_stmt->label);
  if (target_label == NULL) {
    label_error_at("Goto Resolution Error", stmt->loc, "label %.*s has no definition",
                   (int)goto_stmt->label->len, goto_stmt->label->start);
    return AST_WALK_STOP;
  }
  goto_stmt->label = target_label;
  return AST_WALK_CHILDREN;
}

// Resolve goto statements within a block by replacing their labels.
// Returns true on success; false on unresolved labels.
bool resolve_gotos(struct Block* block) {
  struct AstVisitor visitor = { resolve_goto_visit_stmt, NULL, NULL };
  return ast_walk_block(&visitor, block);
}

// -------------------------------- case collection -------------------------------- //

// Record each case and default label in the innermost enclosing switch's case
// list (current_case_list), rejecting duplicates. Each switch collects its own
// list and stores it on the switch statement.
static enum AstWalkAction collect_case_visit_stmt(struct AstVisitor* v, struct Statement* stmt) {
  switch (stmt->type) {
    case CASE_STMT: {
      struct CaseStmt* case_stmt = &stmt->statement.case_stmt;
      if (case_stmt->expr->type != LIT) {
        label_error_at("Case Collection Error", stmt->loc,
                       "case statement with non-constant expression");
        return AST_WALK_STOP;
      }
      int case_value = case_stmt->expr->expr.lit_expr.value.int_val;
      for (struct CaseList* it = current_case_list; it != NULL; it = it->next) {
        if (it->case_label.type == INT_CASE && it->case_label.data == case_value) {
          label_error_at("Case Collection Error", stmt->loc, "duplicate case %d", case_value);
          return AST_WALK_STOP;
        }
      }
      struct CaseList* new_case = arena_alloc(sizeof(struct CaseList));
      new_case->case_label.type = INT_CASE;
      new_case->case_label.data = case_value;
      new_case->next = current_case_list;
      current_case_list = new_case;
      return AST_WALK_CHILDREN;
    }
    case DEFAULT_STMT: {
      for (struct CaseList* it = current_case_list; it != NULL; it = it->next) {
        if (it->case_label.type == DEFAULT_CASE) {
          label_error_at("Case Collection Error", stmt->loc, "duplicate default case");
          return AST_WALK_STOP;
        }
      }
      struct CaseList* new_case = arena_alloc(sizeof(struct CaseList));
      new_case->case_label.type = DEFAULT_CASE;
      new_case->case_label.data = 0;
      new_case->next = current_case_list;
      current_case_list = new_case;
      return AST_WALK_CHILDREN;
    }
    case SWITCH_STMT: {
      struct CaseList* enclosing = current_case_list;
      current_case_list = NULL;
      if (!ast_walk_stmt_children(v, stmt)) {
        return AST_WALK_STOP;
      }
      stmt->statement.switch_stmt.cases = current_case_list;
      current_case_list = enclosing;
      return AST_WALK_SKIP;
    }
    default:
      return AST_WALK_CHILDREN;
  }
}

// Collect case/default labels for every switch in a block.
// Returns true on success; false on case collection errors.
// Case expressions must be literal integers.
bool collect_cases(struct Block* block) {
  struct AstVisitor visitor = { collect_case_visit_stmt, NULL, NULL };
  return ast_walk_block(&visitor, block);
}

// Synthesize a unique label for a switch case value.
// Returns a new Slice containing "switch.case.N".
struct Slice* make_case_label(struct Slice* switch_label, int case_value) {
  // append ".case.<u32>" to current switch label, using unsigned digits to keep labels valid
  uint32_t unsigned_value = (uint32_t)case_value;
  unsigned id_len = u32_len(unsigned_value);
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
    case_label_str[switch_label->len + 6 + id_len - 1 - i] = '0' + (unsigned)(unsigned_value % 10u);
    unsigned_value /= 10u;
  }

  struct Slice* case_label = (struct Slice*)arena_alloc(sizeof(struct Slice));
  case_label->start = case_label_str;
  case_label->len = switch_label->len + 6 + id_len;

  return case_label;
}
