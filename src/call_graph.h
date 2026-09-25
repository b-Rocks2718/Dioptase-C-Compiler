#ifndef CALL_GRAPH_H
#define CALL_GRAPH_H

#include "TAC.h"

// Link one predecessor or successor into a CFG adjacency list.
struct CallGraphNodeEntry {
  struct CallGraphNode* node;
  struct CallGraphNodeEntry* next;
};

// Own an insertion-ordered list of CFG edges.
struct CallGraphNodeList {
  struct CallGraphNodeEntry* head;
  struct CallGraphNodeEntry* tail;
};

// Reserve the call-graph result type while interprocedural analysis is unimplemented.
struct CallGraph {
  struct CallGraphNodeList nodes; // one for each function
  unsigned num_nodes; // number of functions in the call graph
};

// Represent one control-flow node with bidirectional edges and an optional TAC block.
struct CallGraphNode {
  // caller and callee links are bidirectional.
  struct CallGraphNodeList callers;
  struct CallGraphNodeList callees;

  struct TACInstrList body; // instructions making up the function body
};

// Build interprocedural call relationships; currently unimplemented and returns NULL.
struct CallGraph* build_callgraph(struct TACProg* program);

#endif // CALL_GRAPH_H
