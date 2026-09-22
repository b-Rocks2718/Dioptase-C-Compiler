#ifndef DEAD_STORE_ELIM_H
#define DEAD_STORE_ELIM_H
#include "cfg.h"
#include "slice.h"

// Performs dead store elimination on the given control flow graph (CFG).
struct CFG* dead_store_elim(struct CFG* cfg, struct SliceList static_vars, struct SliceList aliased_vars);

#endif // DEAD_STORE_ELIM_H