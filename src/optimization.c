#include "optimization.h"
#include "arena.h"
#include "TAC.h"
#include "constant_fold.h"
#include "dead_code_elim.h"
#include "copy_prop.h"
#include "dead_store_elim.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    struct TACInstr* post_const_fold_body = body;
    if (options.constant_fold) {
      post_const_fold_body = constant_fold(body);
    }

    struct CFG* cfg = build_cfg(post_const_fold_body);

    if (options.dead_code_elim) {
      cfg = dead_code_elim(cfg);
    }

    if (options.copy_prop){
      cfg = copy_prop(cfg);
    }

    if (options.dead_store_elim) {
      cfg = dead_store_elim(cfg);
    }

    struct TACInstr* new_body = rebuild_body(cfg);
    if (compare_bodies(new_body, body)) {
      return new_body;
    }
    body = new_body;
  }
}
