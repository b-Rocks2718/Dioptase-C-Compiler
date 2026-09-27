#ifndef INLINING_H
#define INLINING_H

#include "call_graph.h"

#define MAX_INLINE_CALLEE_INSTRS 16
#define MAX_INLINE_CALLER_INSTRS 128

#define NUM_INLINE_ITERS 2

// Perform function inlining on the given call graph.
void perform_inlining(struct CallGraph* cg);

#endif // INLINING_H