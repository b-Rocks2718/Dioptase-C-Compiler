#ifndef OPTIMIZATION_H
#define OPTIMIZATION_H

#include <stdbool.h>
#include "TAC.h"
#include "cfg.h"

// Configure optimization options.
struct OptimizationOptions {
  // within function body
  bool constant_fold;
  bool dead_code_elim;
  bool copy_prop;
  bool dead_store_elim;
  bool tail_call_opt;

  // across function calls
  bool inline_opt;

  // low-level optimizations
  bool peephole_opt;
  bool reg_alloc;
};

void optimize(struct TACProg* prog, struct OptimizationOptions options);

struct TACInstr* optimize_body(struct TACInstr* body, struct OptimizationOptions options);

// Collect address-taken variables in body together with every static variable.
struct SliceList get_aliased_vars(struct TACInstr* body);

#endif // OPTIMIZATION_H