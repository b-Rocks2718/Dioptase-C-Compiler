#ifndef REGALLOC_H
#define REGALLOC_H

#include "asm_gen.h"
#include "interference_graph.h"

// Represents a mapping from pseudo-registers to physical registers.
struct RegisterMap {
  unsigned placeholder; // TODO
};

struct RegisterMap* create_register_map(struct InterferenceGraph* ig);

// Assign pseudos to physical registers. Runs between instruction selection
// (prog_to_asm) and assign_stack_slots: a pseudo replaced by a register here
// never receives a stack slot. Not implemented yet, so every pseudo currently
// falls through to a stack slot.
void allocate_registers(struct AsmProg* prog);

#endif // REGALLOC_H
