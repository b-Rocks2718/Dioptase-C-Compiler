#ifndef REGALLOC_H
#define REGALLOC_H

#include "asm_gen.h"
#include "interference_graph.h"

// Where coloring placed each register-candidate pseudo of one function.
// Pseudo ids come from the interference graph's pseudo_ids index, so the map
// is only valid while that graph is alive.
struct RegisterMap {
  const struct SliceIndex* pseudo_ids; // borrowed from the interference graph
  enum Reg* regs; // regs[id]: register for pseudo id, or R0 if it was spilled
  size_t num_pseudos;
};

// Translate ig's coloring into registers and record in
// func->callee_saved_regs every callee-saved register a pseudo received.
struct RegisterMap* build_register_map(struct InterferenceGraph* ig, struct AsmFunc* func);

// Register assigned to the pseudo named name, or R0 when it has none (it was
// spilled or was never a register candidate) and stays in memory. R0 works as
// the sentinel because it is hardwired to zero and never allocated.
enum Reg register_map_get(const struct RegisterMap* map, const struct Slice* name);

// Release a map from build_register_map.
void destroy_register_map(struct RegisterMap* map);

// Assign pseudos to physical registers. Runs between instruction selection
// (prog_to_asm) and assign_stack_slots: a pseudo replaced by a register here
// never receives a stack slot. Records each function's callee-saved register
// use in AsmFunc.callee_saved_regs for the prologue and epilogue.
void allocate_registers(struct AsmProg* prog);

#endif // REGALLOC_H
