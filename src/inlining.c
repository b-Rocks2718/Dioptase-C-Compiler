#include "inlining.h"
#include "TAC.h"

#include <stdio.h>

// Inline a single callsite within the caller function.
static void inline_callsite(
    struct CallGraphNode* caller_node, 
    struct TACInstr* call_instr, 
    struct TACFunc* callee) {
  
  // make end label
  struct Slice* end_label = 
    tac_make_label(caller_node->func->name, "inline_end");

  // generate a copy of the func, but replace the original vars with
  // unique variable names, and replacing returns with jumps to the end label
  struct TACFunc* unique_copy = malloc(sizeof(struct TACFunc));


  // generate TAC copying args into params

  
  /* caller node num instrs += callee->num_instrs */
}

// Perform inlining for a single call graph node.
static void perform_inlining_for_node(struct CallGraph* cg, struct CallGraphNode* node){
  // iterate over function body looking for inlining opportunities
  for (struct TACInstr* instr = node->func->body.head; instr != NULL; instr = instr->next) {
    // for now, only consider inlining normal direct calls. ignore tail calls or indirect calls
    if (instr->type == TACCALL && instr->instr.tac_call.func_name != NULL) {
      // check if the callsite makes sense to inline
      struct CallGraphNode* callee_node = find_call_graph_node(cg, instr->instr.tac_call.func_name);
      if (callee_node == NULL) {
        continue; // callee not found in the call graph, skip inlining
      }
      if (callee_node->num_instrs > MAX_INLINE_CALLEE_INSTRS) {
        continue; // callee too large to inline
      }
      if (node->num_instrs > MAX_INLINE_CALLER_INSTRS) {
        continue; // caller too large to inline into
      }
      // we are good to inline this callsite into the caller
      inline_callsite(node, instr, callee_node->func);
    }
  }
}

// Perform function inlining on the given call graph.
void perform_inlining(struct CallGraph* cg) {
  for (int i = 0; i < NUM_INLINE_ITERS; i++) {
    // iterate multiple times to allow for nested inlining opportunities
    for (struct CallGraphEntry* entry = cg->nodes.head; entry != NULL; entry = entry->next) {
      struct CallGraphNode* node = entry->node;
      perform_inlining_for_node(cg, node);
    }
  }
}
