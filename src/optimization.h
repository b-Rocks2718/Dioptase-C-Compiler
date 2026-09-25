#ifndef OPTIMIZATION_H
#define OPTIMIZATION_H

#include <stdbool.h>
#include "TAC.h"
#include "cfg.h"

// Select compiler optimization passes. Body passes operate on one function's
// TAC/CFG; the remaining fields reserve switches for later pipeline stages.
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

// Apply enabled body optimizations to every function until reaching a fixed
// point. This is a no-op when no implemented body pass is selected.
void optimize(struct TACProg* prog, struct OptimizationOptions options);

// Collect address-taken variables in body together with every translation-unit
// static variable. Returned list nodes are allocated from the current arena
// (the optimizer's per-function scratch arena when called from optimize).
struct SliceList get_aliased_vars(struct TACInstr* body, struct SliceList static_vars);

// Collect all static variables in the translation unit exactly once per name.
// Returned list nodes are allocated from the current arena.
struct SliceList get_static_vars(void);

#endif // OPTIMIZATION_H
