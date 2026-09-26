#include "call_graph.h"
#include "arena.h"

// Build caller-to-callee edges; currently unimplemented and returns NULL.
struct CallGraph* build_callgraph(struct TACProg* program) {
  struct CallGraph* cg = arena_alloc(sizeof(struct CallGraph));
  
  // iterate over all functions and add them to call graph
  for (struct TopLevel* top = program->head; top != NULL; top = top->next) {
    if (top->type != FUNC) {
      continue;
    }

    struct CallGraphNode* node = arena_alloc(sizeof(struct CallGraphNode));
    //node->body = top->top.tac_func.body;
    //node->num_callees = 0;
    //node->callees = NULL;
  }
}
