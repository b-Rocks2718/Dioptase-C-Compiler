#ifndef CALL_GRAPH_H
#define CALL_GRAPH_H

#include "TAC.h"

// The call graph stores todo.
struct CallGraph {
  unsigned todo;
};

struct CallGraph* build_callgraph(struct TACProg* program);

#endif // CALL_GRAPH_H
