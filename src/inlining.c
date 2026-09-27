#include "inlining.h"
#include "TAC.h"
#include "unique_name.h"
#include "identifier_map.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>

// Inline a single callsite within the caller function.
static void inline_callsite(
    struct CallGraphNode* caller_node, 
    struct TACInstr* call_instr, 
    struct TACFunc* callee) {
  
  // make end label
  struct Slice* end_label = 
    tac_make_label(caller_node->func->name, "inline_end");

  // create identifier map so we can replace original identifiers with unique ones
  struct IdentMap* id_map = create_ident_map(16); // inlined functions are small, small hashmap should work

  // generate a copy of the func, but replace the original vars with
  // unique variable names, and replacing returns with jumps to the end label
  struct TACFunc* unique_copy = arena_alloc(sizeof(struct TACFunc));
  unique_copy->name = callee->name;
  unique_copy->global = callee->global;
  unique_copy->num_params = callee->num_params;
  unique_copy->params = arena_alloc(sizeof(struct TACVar*) * callee->num_params);
  for (int i = 0; i < callee->num_params; i++) {
    unique_copy->params[i] = make_unique(callee->params[i], "inline");

    // only the first three fields matter here, we will not use the other data stored in the identmap
    ident_map_insert(id_map, callee->params[i], unique_copy->params[i], false, -1, false, 0);
  }

  // generate TAC copying args into params
  //for (int i = 0; i < callee->num_params; i++) {
  //  struct TACInstr* arg_copy = arena_alloc(sizeof(struct TACInstr));
  //  arg_copy->type = TACCOPY;
  //  arg_copy->instr.tac_copy.src = tac_make_var(call_instr->instr.tac_call.args[i].val.var_name, call_instr->instr.tac_call.args[i].val_type);
  //  arg_copy->instr.tac_copy.dst = unique_copy->params[i];
  //  // insert the copy instruction before the call instruction in the caller's body
  //  arg_copy->next = call_instr;
  //  arg_copy->prev = call_instr->prev;
  //  if (call_instr->prev) {
  //    call_instr->prev->next = arg_copy;
  //  } else {
  //    caller_node->func->body.head = arg_copy;
  //  }
  //  call_instr->prev = arg_copy;
  //}
  
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
