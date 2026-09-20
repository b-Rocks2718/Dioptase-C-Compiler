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

struct TACInstr* constant_fold(struct TACInstr* body);

struct CFG* dead_code_elim(struct CFG* cfg);

struct CFG* copy_prop(struct CFG* cfg);

struct CFG* dead_store_elim(struct CFG* cfg);

#endif // OPTIMIZATION_H