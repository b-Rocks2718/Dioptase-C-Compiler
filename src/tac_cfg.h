#ifndef TAC_CFG_H
#define TAC_CFG_H

#include "cfg.h"
#include "TAC.h"

// Partition a TAC function body into basic blocks and connect its control-flow edges.
struct CFG* build_cfg(struct TACInstr* body);

// Print an ASCII CFG visualization, including TAC and labeled outgoing edges.
void print_cfg(const struct CFG* cfg);

// Rebuild a linear TAC body from the CFG's current basic blocks.
struct TACInstrList rebuild_body(struct CFG* cfg);

// After removing blocks from the CFG, nodes may contain dangling edges. 
// Repair the CFG to maintain consistency. Removes empty blocks as a side effect.
struct CFG* repair_cfg(struct CFG* cfg);

#endif // TAC_CFG_H
