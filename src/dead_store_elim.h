#ifndef DEAD_STORE_ELIM_H
#define DEAD_STORE_ELIM_H
#include "cfg.h"
#include "slice.h"

// Perform dead-store elimination on cfg. Liveness uses pass-local indexed
// bitsets; static_vars remain live at function exit and aliased_vars become
// live across calls and loads.
struct CFG* dead_store_elim(struct CFG* cfg, struct SliceList static_vars, struct SliceList aliased_vars);

#endif // DEAD_STORE_ELIM_H
