#ifndef CALL_GRAPH_H
#define CALL_GRAPH_H

#include "TAC.h"

// Link one function into a call-graph adjacency list.
struct CallGraphEntry {
  struct CallGraphNode* node;
  struct CallGraphEntry* next;
};

// Own an insertion-ordered list of call-graph nodes or edges.
struct CallGraphNodeList {
  struct CallGraphEntry* head;
  struct CallGraphEntry* tail;
};

// Call graph for the function definitions in the current translation unit.
struct CallGraph {
  struct CallGraphNodeList nodes;
};

// Represent one function definition and its bidirectional call relationships.
struct CallGraphNode {
  // Every direct edge appears in both lists. Repeated calls produce one edge.
  struct CallGraphNodeList callers;
  struct CallGraphNodeList callees;

  struct TACInstrList body;

  struct Slice* func_name;
  bool contains_indirect_calls;
};

// Build direct caller-to-callee relationships between function definitions.
// Direct calls outside this translation unit are intentionally not represented.
struct CallGraph build_call_graph(struct TACProg* program);

// Print a stable adjacency-list visualization of direct and indirect calls.
void print_call_graph(const struct CallGraph* call_graph);

#endif // CALL_GRAPH_H
