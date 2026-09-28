#include "inlining.h"
#include "TAC.h"
#include "unique_name.h"
#include "identifier_map.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>

static struct Slice* replace_var(struct Slice* var, struct IdentMap* id_map) {
  // implementation of replacing variable identifiers with unique ones
  // for now, just return the original variable
  return var;
}

static struct Val* replace_val(struct Val* val, struct IdentMap* id_map) {
  // implementation of replacing identifiers with unique ones
  // for now, just return the original value
  return val;
}

static struct Slice* replace_label(struct Slice* label, struct IdentMap* id_map) {
  // implementation of replacing labels with unique ones
  // for now, just return the original label
  return label;
}

static void replace_identifiers_and_labels(struct TACInstr* instr, struct IdentMap* id_map) {
  switch (instr->type) {
    case TACRETURN: {
      // should not happen, as returns are replaced with jumps to the end label
      fprintf(stderr, "Error: unexpected return instruction during inlining\n");
      exit(EXIT_FAILURE);
    }
    case TACUNARY: {
      instr->instr.tac_unary.src = replace_val(instr->instr.tac_unary.src, id_map);
      instr->instr.tac_unary.dst = replace_val(instr->instr.tac_unary.dst, id_map);
      break;
    }
    case TACBINARY: {
      instr->instr.tac_binary.src1 = replace_val(instr->instr.tac_binary.src1, id_map);
      instr->instr.tac_binary.src2 = replace_val(instr->instr.tac_binary.src2, id_map);
      instr->instr.tac_binary.dst = replace_val(instr->instr.tac_binary.dst, id_map);
      break;
    }
    case TACCOND_JUMP: {
      instr->instr.tac_cond_jump.src1 = replace_val(instr->instr.tac_cond_jump.src1, id_map);
      instr->instr.tac_cond_jump.src2 = replace_val(instr->instr.tac_cond_jump.src2, id_map);
      instr->instr.tac_cond_jump.label = replace_label(instr->instr.tac_cond_jump.label, id_map);
      break;
    }
    case TACJUMP: {
      instr->instr.tac_jump.label = replace_label(instr->instr.tac_jump.label, id_map);
      break;
    }
    case TACLABEL: {
      instr->instr.tac_label.label = replace_label(instr->instr.tac_label.label, id_map);
      break;
    }
    case TACCOPY: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, id_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, id_map);
      break;
    }
    case TACVOLATILE_READ: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, id_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, id_map);
      break;
    }
    case TACVOLATILE_WRITE: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, id_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, id_map);
      break;
    }
    case TACVOLATILE_LOAD: {
      instr->instr.tac_load.src_ptr = replace_val(instr->instr.tac_load.src_ptr, id_map);
      instr->instr.tac_load.dst = replace_val(instr->instr.tac_load.dst, id_map);
      break;
    }
    case TACVOLATILE_STORE: {
      instr->instr.tac_store.src = replace_val(instr->instr.tac_store.src, id_map);
      instr->instr.tac_store.dst_ptr = replace_val(instr->instr.tac_store.dst_ptr, id_map);
      break;
    }
    case TACVOLATILE_COPY_TO_OFFSET: {
      instr->instr.tac_copy_to_offset.src = replace_val(instr->instr.tac_copy_to_offset.src, id_map);
      instr->instr.tac_copy_to_offset.dst = replace_var(instr->instr.tac_copy_to_offset.dst, id_map);
      break;
    }
    case TACVOLATILE_COPY_FROM_OFFSET: {
      instr->instr.tac_copy_from_offset.src = replace_var(instr->instr.tac_copy_from_offset.src, id_map);
      instr->instr.tac_copy_from_offset.dst = replace_val(instr->instr.tac_copy_from_offset.dst, id_map);
      break;
    }
    case TACCALL: {
      if (instr->instr.tac_call.dst != NULL){
        instr->instr.tac_call.dst = replace_val(instr->instr.tac_call.dst, id_map);
      }
      for (int i = 0; i < instr->instr.tac_call.num_args; i++) {
        instr->instr.tac_call.args[i] = *replace_val(&instr->instr.tac_call.args[i], id_map);
      }
      break;
    }
    case TACCALL_INDIRECT: {
      if (instr->instr.tac_call_indirect.dst != NULL){
        instr->instr.tac_call_indirect.dst = replace_val(instr->instr.tac_call_indirect.dst, id_map);
      }
      instr->instr.tac_call_indirect.func = replace_val(instr->instr.tac_call_indirect.func, id_map);
      for (int i = 0; i < instr->instr.tac_call_indirect.num_args; i++) {
        instr->instr.tac_call_indirect.args[i] = *replace_val(&instr->instr.tac_call_indirect.args[i], id_map);
      }
      break;
    }
    case TACTAIL_CALL:
    case TACTAIL_CALL_INDIRECT: {
      // for now we do not inline functions containing tail calls
      fprintf(stderr, "Tail calls should not be inlined.\n");
      exit(EXIT_FAILURE);
    }
    case TACGET_ADDRESS: {
      instr->instr.tac_get_address.dst = replace_val(instr->instr.tac_get_address.dst, id_map);
      instr->instr.tac_get_address.src = replace_val(instr->instr.tac_get_address.src, id_map);
      break;
    }
    case TACLOAD: {
      instr->instr.tac_load.src_ptr = replace_val(instr->instr.tac_load.src_ptr, id_map);
      instr->instr.tac_load.dst = replace_val(instr->instr.tac_load.dst, id_map);
      break;
    }
    case TACSTORE: {
      instr->instr.tac_store.src = replace_val(instr->instr.tac_store.src, id_map);
      instr->instr.tac_store.dst_ptr = replace_val(instr->instr.tac_store.dst_ptr, id_map);
      break;
    }
    case TACCOPY_TO_OFFSET: {
      instr->instr.tac_copy_to_offset.src = replace_val(instr->instr.tac_copy_to_offset.src, id_map);
      instr->instr.tac_copy_to_offset.dst = replace_var(instr->instr.tac_copy_to_offset.dst, id_map);
      break;
    }
    case TACCOPY_FROM_OFFSET: {
      instr->instr.tac_copy_from_offset.src = replace_var(instr->instr.tac_copy_from_offset.src, id_map);
      instr->instr.tac_copy_from_offset.dst = replace_val(instr->instr.tac_copy_from_offset.dst, id_map);
      break;
    }
    case TACBOUNDARY: {
      // no change needed
      break;
    }
    case TACTRUNC: {
      instr->instr.tac_trunc.src = replace_val(instr->instr.tac_trunc.src, id_map);
      instr->instr.tac_trunc.dst = replace_val(instr->instr.tac_trunc.dst, id_map);
      break;
    }
    case TACEXTEND: {
      instr->instr.tac_extend.src = replace_val(instr->instr.tac_extend.src, id_map);
      instr->instr.tac_extend.dst = replace_val(instr->instr.tac_extend.dst, id_map);
      break;
    }
  }
}

// Inline a single callsite within the caller function.
// return the last instruction of the inlined code
static struct TACInstr* inline_callsite(
    struct CallGraphNode* caller_node, 
    struct TACInstr* call_instr, 
    struct TACInstr* prev_instr,
    struct TACFunc* callee) {

  // make end label
  struct Slice* end_label = 
    tac_make_label(caller_node->func->name, "inline_end");

  // create identifier map so we can replace original identifiers with unique ones
  struct IdentMap* id_map = create_ident_map(16); // inlined functions are small, small hashmap should work

  // generate unique parameter names for the callee
  struct Slice** params = arena_alloc(sizeof(struct Slice*) * callee->num_params);
  for (int i = 0; i < callee->num_params; i++) {
    params[i] = make_unique(callee->params[i], "inline");

    // only the first three fields matter here, we will not use the other data stored in the identmap
    ident_map_insert(id_map, callee->params[i], params[i], false, -1, false, 0);
  }

  // generate TAC copying args into params
  for (int i = 0; i < callee->num_params; i++) {
    struct TACInstr* arg_copy = arena_alloc(sizeof(struct TACInstr));
    struct Val arg = call_instr->instr.tac_call.args[i];
    arg_copy->type = TACCOPY;
    arg_copy->instr.tac_copy.src = copy_val(&arg);
    arg_copy->instr.tac_copy.dst = tac_make_var(params[i], arg.type);
    // insert the copy instruction before the call instruction in the caller's body
    arg_copy->next = call_instr;
    if (prev_instr) {
      prev_instr->next = arg_copy;
    } else {
      caller_node->func->body.head = arg_copy;
    }
    prev_instr = arg_copy;
    
    caller_node->num_instrs++;
  }
  
  // insert the instructions for the inlined callee
  // replace return instructions in the callee with jumps to the end label
  // replace var names with new unique names
  // replace label names with new unique names
  for (struct TACInstr* callee_instr = callee->body.head; callee_instr != NULL; callee_instr = callee_instr->next) {
    if (callee_instr->type == TACRETURN) {
      // replace return with copy into call result and jump to end label
      if (call_instr->instr.tac_call.dst != NULL){
        struct TACInstr* ret_copy = tac_instr_create(TACCOPY);
        ret_copy->instr.tac_copy.src = copy_val(callee_instr->instr.tac_return.src);
        ret_copy->instr.tac_copy.dst = call_instr->instr.tac_call.dst;
        // insert the copy instruction before the call instruction in the caller's body
        ret_copy->next = call_instr;
        if (prev_instr) {
          prev_instr->next = ret_copy;
        } else {
          caller_node->func->body.head = ret_copy;
        }
        prev_instr = ret_copy;
        caller_node->num_instrs++;
      } 

      struct TACInstr* jump_instr = tac_instr_create(TACJUMP);
      jump_instr->instr.tac_jump.label = end_label;
      // insert the jump instruction before the call instruction in the caller's body
      jump_instr->next = call_instr;
      if (prev_instr) {
        prev_instr->next = jump_instr;
      } else {
        caller_node->func->body.head = jump_instr;
      }
      prev_instr = jump_instr;
      caller_node->num_instrs++;
    } else {
      // use same instruction, but replace identifiers and labels with unique ones
      struct TACInstr* new_instr = copy_instr(callee_instr);

      replace_identifiers_and_labels(new_instr, id_map);

      // insert the new instruction before the call instruction in the caller's body
      new_instr->next = call_instr;
      if (prev_instr) {
        prev_instr->next = new_instr;
      } else {
        caller_node->func->body.head = new_instr;
      }
      prev_instr = new_instr;
      caller_node->num_instrs++;
    }
  }

  // insert the end label after the inlined code
  struct TACInstr* end_label_instr = tac_instr_create(TACLABEL);
  end_label_instr->instr.tac_label.label = end_label;

  // insert the end label after the inlined code
  // remove the original call instruction from the caller's body
  end_label_instr->next = call_instr->next;
  if (prev_instr) {
    prev_instr->next = end_label_instr;
  } else {
    caller_node->func->body.head = end_label_instr;
  }
  if (call_instr->next == NULL) {
    // normally this is the case where we update the tail:
    //    caller_node->func->body.last = end_label_instr;
    // but actually this is a compiler error because functions should end with a 
    // return, not a call

    fprintf(stderr, "Error: function should end with a return, not a call\n");
    exit(EXIT_FAILURE);
  }
  // no change to count, we added end label and removed the call

  return end_label_instr;
}

// Perform inlining for a single call graph node.
static void perform_inlining_for_node(struct CallGraph* cg, struct CallGraphNode* node){
  // iterate over function body looking for inlining opportunities
  struct TACInstr* prev = NULL;
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
      instr = inline_callsite(node, instr, prev, callee_node->func);
    }
    prev = instr;
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
