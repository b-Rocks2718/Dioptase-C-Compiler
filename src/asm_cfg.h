#ifndef ASM_CFG_H
#define ASM_CFG_H

#include "asm_gen.h"

// Control Flow Graph for assembly instructions.
struct AsmCFG {
  unsigned placeholder; // TODO
};

// Builds the control flow graph for the given assembly instructions.
struct AsmCFG* build_asm_cfg(struct AsmInstr* instrs);

// Annotate each basic block with the live vars
void analyze_liveness(struct AsmCFG* cfg);

#endif // ASM_CFG_H