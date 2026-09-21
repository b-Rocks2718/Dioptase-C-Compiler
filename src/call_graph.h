#ifndef CALL_GRAPH_H
#define CALL_GRAPH_H

#include "TAC.h"

// Reserve the call-graph result type while interprocedural analysis is unimplemented.
struct CallGraph {
  unsigned todo;
};

// Build interprocedural call relationships; currently unimplemented and returns NULL.
struct CallGraph* build_callgraph(struct TACProg* program);

#endif // CALL_GRAPH_H
