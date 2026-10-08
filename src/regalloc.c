#include "regalloc.h"

#include "codegen.h"
#include "interference_graph.h"

// Creates a new register map for the given interference graph
struct RegisterMap* build_register_map(struct InterferenceGraph* ig) {
  return NULL; // placeholder implementation
}

// Assigns physical registers to the given assembly instructions based on the register map.
void assign_registers(struct AsmInstr* instrs, struct RegisterMap* reg_map) {
  // placeholder implementation
}

// Allocates registers for the body of a function based on the interference graph.
void alloc_body_registers(struct AsmInstr* instrs) {
  struct InterferenceGraph* ig = build_interference_graph(instrs);
  add_spill_costs(ig, instrs);
  color_graph(ig);
  struct RegisterMap* reg_map = build_register_map(ig);
  assign_registers(instrs, reg_map);
}

// No allocation yet: leaving every pseudo in place makes assign_stack_slots
// give each one a frame slot, which is the compiler's current behavior.
void allocate_registers(struct AsmProg* prog) {
  // iterate over functions and allocate registers for each one
  for (struct AsmTopLevel* top = prog->head; top != NULL; top = top->next) {
    // allocate registers for the function if this top-level item is a function
    if (top->type == ASM_FUNC) {
      struct AsmInstr* instrs = top->top.asm_func.body;
      alloc_body_registers(instrs);
    }
  }
}
