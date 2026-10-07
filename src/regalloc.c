#include "regalloc.h"

// No allocation yet: leaving every pseudo in place makes assign_stack_slots
// give each one a frame slot, which is the compiler's current behavior.
void allocate_registers(struct AsmProg* prog) {
  (void)prog;
}
