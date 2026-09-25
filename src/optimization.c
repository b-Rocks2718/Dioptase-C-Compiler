#include "optimization.h"
#include "arena.h"
#include "TAC.h"
#include "constant_fold.h"
#include "dead_code_elim.h"
#include "copy_prop.h"
#include "dead_store_elim.h"
#include "slice.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Append name if it is not already present in list.
static void add_aliased_var(struct SliceList* list, struct Slice* name) {
  if (name == NULL || slice_list_contains(*list, name)) {
    return;
  }
  slice_list_add(list, name);
}

// Return whether any implemented TAC/CFG optimization needs to run.
// Tail-call lowering happens while TAC is generated, and the remaining
// reserved options do not have passes yet, so they do not require a CFG.
static bool has_body_optimization(struct OptimizationOptions options) {
  return options.constant_fold || options.dead_code_elim || options.copy_prop ||
         options.dead_store_elim;
}

// Extend list with each unique name in additions.
static void add_unique_vars(struct SliceList* list, struct SliceList additions) {
  for (struct SliceListNode* node = additions.head; node != NULL; node = node->next) {
    add_aliased_var(list, node->slice);
  }
}

// Collect address-taken variables in body together with the already-computed
// program-wide static-variable set. Optimizer passes cannot introduce a new
// TACGET_ADDRESS, so this conservative set is invariant across fixed-point
// iterations for one function.
static struct SliceList get_aliased_vars_with_statics(struct TACInstr* body,
                                                      struct SliceList static_vars) {
  struct SliceList list = {NULL, NULL};
  add_unique_vars(&list, static_vars);

  for (struct TACInstr* instr = body; instr != NULL; instr = instr->next) {
    if (instr->type == TACGET_ADDRESS) {
      // src is a variable: typechecking rejects taking the address of a literal
      struct Val* src = instr->instr.tac_get_address.src;
      if (src != NULL && src->val_type == VARIABLE) {
        add_aliased_var(&list, src->val.var_name);
      }
    }
  }
  return list;
}

// Collect address-taken variables in body together with every static variable.
// Each name appears at most once.
struct SliceList get_aliased_vars(struct TACInstr* body) {
  return get_aliased_vars_with_statics(body, get_static_vars());
}

// Collect every static variable in the translation unit. Each name appears at
// most once, and the returned list nodes remain arena-owned.
struct SliceList get_static_vars(void) {
  struct SliceList list = {NULL, NULL};

  if (global_symbol_table != NULL) {
    for (size_t i = 0; i < global_symbol_table->size; i++) {
      for (struct SymbolEntry* entry = global_symbol_table->arr[i];
           entry != NULL;
           entry = entry->next) {
        if (entry->attrs != NULL && entry->attrs->attr_type == STATIC_ATTR) {
          add_aliased_var(&list, entry->key);
        }
      }
    }
  }

  return list;
}

static struct TACInstrList optimize_body_with_statics(
    struct TACInstrList body,
    struct OptimizationOptions options,
    struct SliceList static_vars);

// Run the enabled body optimizations on every function until each TAC body
// reaches a fixed point. Translation-unit static names are collected once and
// reused, while address-taken names are invariant for each function.
void optimize(struct TACProg* prog, struct OptimizationOptions options) {
  if (prog == NULL || !has_body_optimization(options)) {
    return;
  }

  // Static storage duration is a translation-unit property. Reuse this set
  // across every function instead of rescanning the symbol table per function.
  struct SliceList static_vars = get_static_vars();
  for (struct TopLevel* top = prog->head; top != NULL; top = top->next) {
    if (top->type == FUNC) {
      top->top.tac_func.body =
          optimize_body_with_statics(top->top.tac_func.body, options, static_vars);
    }
  }
}

// Run the enabled optimization passes over one function body while reusing the
// translation-unit static-variable set supplied by the caller.
static struct TACInstrList optimize_body_with_statics(
    struct TACInstrList body,
    struct OptimizationOptions options,
    struct SliceList static_vars) {
  if (body.head == NULL || !has_body_optimization(options)) {
    return body;
  }

  struct SliceList aliased_vars =
      get_aliased_vars_with_statics(body.head, static_vars);
  while (true) {
    struct TACInstrList post_const_fold_body = body;
    if (options.constant_fold) {
      post_const_fold_body = constant_fold(body);
    }

    struct CFG* cfg = build_cfg(post_const_fold_body.head);

    if (options.dead_code_elim) {
      cfg = dead_code_elim(cfg);
    }

    if (options.copy_prop) {
      cfg = copy_prop(cfg, aliased_vars);
    }

    if (options.dead_store_elim) {
      cfg = dead_store_elim(cfg, static_vars, aliased_vars);
    }

    struct TACInstrList new_body = rebuild_body(cfg);
    if (compare_bodies(new_body.head, body.head)) {
      return new_body;
    }
    body = new_body;
  }
}

// Run the enabled optimization passes over one standalone function body.
struct TACInstrList optimize_body(struct TACInstrList body,
                                  struct OptimizationOptions options) {
  return optimize_body_with_statics(body, options, get_static_vars());
}
