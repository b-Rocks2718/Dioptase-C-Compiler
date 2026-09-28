#ifndef INLINING_H
#define INLINING_H

#include "call_graph.h"

#define MAX_INLINE_CALLEE_INSTRS 16
#define MAX_INLINE_CALLER_INSTRS 128

#define NUM_INLINE_ITERS 1

/*
  Current inlining policy:
  - Only inline functions that do not recurse.
  - Only inline functions with fewer than MAX_INLINE_CALLEE_INSTRS instructions.
  - Only inline call sites where the caller has fewer than MAX_INLINE_CALLER_INSTRS instructions.
  - Do not inline functions that contain tail calls.
*/

// Perform function inlining on the given call graph.
void perform_inlining(struct CallGraph* cg);

#endif // INLINING_H