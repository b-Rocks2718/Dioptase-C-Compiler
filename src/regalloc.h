#ifndef REGALLOC_H
#define REGALLOC_H

#include "asm_gen.h"

// Assign pseudos to physical registers. Runs between instruction selection
// (prog_to_asm) and assign_stack_slots: a pseudo replaced by a register here
// never receives a stack slot. Not implemented yet, so every pseudo currently
// falls through to a stack slot.
void allocate_registers(struct AsmProg* prog);

#endif // REGALLOC_H
