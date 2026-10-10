#ifndef ASM_CFG_H
#define ASM_CFG_H

#include "asm_gen.h"
#include "cfg.h"

// Partition an ASM function body into basic blocks and connect their
// control-flow edges, using the same construction as the TAC CFG (cfg.h).
// Blocks are ranges asm_head..asm_last of instrs itself, not copies, so passes
// can rewrite operands in place through the CFG. Inserting, removing, or
// reordering instructions invalidates it.
struct CFG* build_asm_cfg(struct AsmInstr* instrs);

// Print an ASM CFG: each block's instructions, its outgoing edges, and an
// ASCII visualization.
void print_asm_cfg(const struct CFG* cfg);

#endif // ASM_CFG_H
