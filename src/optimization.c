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

// Collect address-taken variables in body together with every static variable.
// Each name appears at most once.
struct SliceList get_aliased_vars(struct TACInstr* body) {
  struct SliceList list = {NULL, NULL};

  if (global_symbol_table != NULL) {
    for (size_t i = 0; i < global_symbol_table->size; i++) {
      for (struct SymbolEntry* entry = global_symbol_table->arr[i];
           entry != NULL;
           entry = entry->next) {
        if (is_static_var(entry->key)) {
          add_aliased_var(&list, entry->key);
        }
      }
    }
  }

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

// iterate over each function and optimize its body
void optimize(struct TACProg* prog, struct OptimizationOptions options) {
  if (prog == NULL) {
    return;
  }
  for (struct TopLevel* top = prog->head; top != NULL; top = top->next) {
    if (top->type == FUNC) {
      struct TACInstr* body = top->top.tac_func.body;
      top->top.tac_func.body = optimize_body(body, options);
    }
  }
}

// Run the enabled optimization passes over one function body.
struct TACInstr* optimize_body(struct TACInstr* body, struct OptimizationOptions options) {
  if (body == NULL) {
    return body;
  }

  while (true) {
    struct SliceList aliased_vars = get_aliased_vars(body);

    struct TACInstr* post_const_fold_body = body;
    if (options.constant_fold) {
      post_const_fold_body = constant_fold(body);
    }

    struct CFG* cfg = build_cfg(post_const_fold_body);

    if (options.dead_code_elim) {
      cfg = dead_code_elim(cfg);
    }

    if (options.copy_prop){
      cfg = copy_prop(cfg, aliased_vars);
    }

    if (options.dead_store_elim) {
      cfg = dead_store_elim(cfg, aliased_vars);
    }

    struct TACInstr* new_body = rebuild_body(cfg);
    if (compare_bodies(new_body, body)) {
      return new_body;
    }
    body = new_body;
  }
}
