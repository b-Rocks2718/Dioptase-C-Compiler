#include "inlining.h"
#include "TAC.h"
#include "unique_name.h"
#include "label_map.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>

static struct Slice* replace_name(struct Slice* name, struct LabelMap* var_map) {
  // check if name already has a mapping in the var map
  struct Slice* mapped_name = label_map_get(var_map, name);
  if (mapped_name != NULL) {
    return mapped_name;
  }

  // if no mapping exists, create a new unique name and add it to the var map
  struct Slice* unique_name = make_unique(name, "inline");
  label_map_insert(var_map, name, unique_name);
  return unique_name;
}

// Replace a variable name only after the containing value has been detached
// from the callee. copy_instr deliberately shares operand pointers, so
// changing one of those operands in place would corrupt the original function.
static void replace_val_name(struct Val* val, struct LabelMap* var_map) {
  if (val != NULL && val->val_type == VARIABLE) {
    val->val.var_name = replace_name(val->val.var_name, var_map);
  }
}

// Return an independently allocated operand with its variable name remapped.
// Type and Slice storage are immutable and remain shared.
static struct Val* replace_val(const struct Val* val, struct LabelMap* var_map) {
  struct Val* replaced = copy_val(val);
  replace_val_name(replaced, var_map);
  return replaced;
}

// Copy and remap a call argument array. A shallow instruction copy shares this
// array with the callee, so rewriting its elements in place is not safe.
static struct Val* replace_args(const struct Val* args, size_t num_args,
                                struct LabelMap* var_map) {
  if (num_args == 0) {
    return NULL;
  }
  if (args == NULL) {
    fprintf(stderr,
            "Inlining error: call has %zu arguments but no argument array\n",
            num_args);
    exit(EXIT_FAILURE);
  }

  struct Val* replaced = arena_alloc(sizeof(*replaced) * num_args);
  if (replaced == NULL) {
    fprintf(stderr,
            "Inlining error: unable to allocate %zu copied call arguments\n",
            num_args);
    exit(EXIT_FAILURE);
  }
  for (size_t i = 0; i < num_args; i++) {
    replaced[i] = args[i];
    replace_val_name(&replaced[i], var_map);
  }
  return replaced;
}

static void replace_identifiers_and_labels(struct TACInstr* instr,
    struct LabelMap* var_map, struct LabelMap* label_map) {
  switch (instr->type) {
    case TACRETURN: {
      // should not happen, as returns are replaced with jumps to the end label
      fprintf(stderr, "Error: unexpected return instruction during inlining\n");
      exit(EXIT_FAILURE);
    }
    case TACUNARY: {
      instr->instr.tac_unary.src = replace_val(instr->instr.tac_unary.src, var_map);
      instr->instr.tac_unary.dst = replace_val(instr->instr.tac_unary.dst, var_map);
      break;
    }
    case TACBINARY: {
      instr->instr.tac_binary.src1 = replace_val(instr->instr.tac_binary.src1, var_map);
      instr->instr.tac_binary.src2 = replace_val(instr->instr.tac_binary.src2, var_map);
      instr->instr.tac_binary.dst = replace_val(instr->instr.tac_binary.dst, var_map);
      break;
    }
    case TACCOND_JUMP: {
      instr->instr.tac_cond_jump.src1 = replace_val(instr->instr.tac_cond_jump.src1, var_map);
      instr->instr.tac_cond_jump.src2 = replace_val(instr->instr.tac_cond_jump.src2, var_map);
      instr->instr.tac_cond_jump.label = replace_name(instr->instr.tac_cond_jump.label, label_map);
      break;
    }
    case TACJUMP: {
      instr->instr.tac_jump.label = replace_name(instr->instr.tac_jump.label, label_map);
      break;
    }
    case TACLABEL: {
      instr->instr.tac_label.label = replace_name(instr->instr.tac_label.label, label_map);
      break;
    }
    case TACCOPY: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, var_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, var_map);
      break;
    }
    case TACVOLATILE_READ: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, var_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, var_map);
      break;
    }
    case TACVOLATILE_WRITE: {
      instr->instr.tac_copy.src = replace_val(instr->instr.tac_copy.src, var_map);
      instr->instr.tac_copy.dst = replace_val(instr->instr.tac_copy.dst, var_map);
      break;
    }
    case TACVOLATILE_LOAD: {
      instr->instr.tac_load.src_ptr = replace_val(instr->instr.tac_load.src_ptr, var_map);
      instr->instr.tac_load.dst = replace_val(instr->instr.tac_load.dst, var_map);
      break;
    }
    case TACVOLATILE_STORE: {
      instr->instr.tac_store.src = replace_val(instr->instr.tac_store.src, var_map);
      instr->instr.tac_store.dst_ptr = replace_val(instr->instr.tac_store.dst_ptr, var_map);
      break;
    }
    case TACVOLATILE_COPY_TO_OFFSET: {
      instr->instr.tac_copy_to_offset.src = replace_val(instr->instr.tac_copy_to_offset.src, var_map);
      instr->instr.tac_copy_to_offset.dst = replace_name(instr->instr.tac_copy_to_offset.dst, var_map);
      break;
    }
    case TACVOLATILE_COPY_FROM_OFFSET: {
      instr->instr.tac_copy_from_offset.src = replace_name(instr->instr.tac_copy_from_offset.src, var_map);
      instr->instr.tac_copy_from_offset.dst = replace_val(instr->instr.tac_copy_from_offset.dst, var_map);
      break;
    }
    case TACCALL: {
      if (instr->instr.tac_call.dst != NULL){
        instr->instr.tac_call.dst = replace_val(instr->instr.tac_call.dst, var_map);
      }
      instr->instr.tac_call.args =
          replace_args(instr->instr.tac_call.args,
                       instr->instr.tac_call.num_args, var_map);
      break;
    }
    case TACCALL_INDIRECT: {
      if (instr->instr.tac_call_indirect.dst != NULL){
        instr->instr.tac_call_indirect.dst = replace_val(instr->instr.tac_call_indirect.dst, var_map);
      }
      instr->instr.tac_call_indirect.func = replace_val(instr->instr.tac_call_indirect.func, var_map);
      instr->instr.tac_call_indirect.args =
          replace_args(instr->instr.tac_call_indirect.args,
                       instr->instr.tac_call_indirect.num_args, var_map);
      break;
    }
    case TACTAIL_CALL:
    case TACTAIL_CALL_INDIRECT: {
      // for now we do not inline functions containing tail calls
      fprintf(stderr, "Tail calls should not be inlined.\n");
      exit(EXIT_FAILURE);
    }
    case TACGET_ADDRESS: {
      instr->instr.tac_get_address.dst = replace_val(instr->instr.tac_get_address.dst, var_map);
      instr->instr.tac_get_address.src = replace_val(instr->instr.tac_get_address.src, var_map);
      break;
    }
    case TACLOAD: {
      instr->instr.tac_load.src_ptr = replace_val(instr->instr.tac_load.src_ptr, var_map);
      instr->instr.tac_load.dst = replace_val(instr->instr.tac_load.dst, var_map);
      break;
    }
    case TACSTORE: {
      instr->instr.tac_store.src = replace_val(instr->instr.tac_store.src, var_map);
      instr->instr.tac_store.dst_ptr = replace_val(instr->instr.tac_store.dst_ptr, var_map);
      break;
    }
    case TACCOPY_TO_OFFSET: {
      instr->instr.tac_copy_to_offset.src = replace_val(instr->instr.tac_copy_to_offset.src, var_map);
      instr->instr.tac_copy_to_offset.dst = replace_name(instr->instr.tac_copy_to_offset.dst, var_map);
      break;
    }
    case TACCOPY_FROM_OFFSET: {
      instr->instr.tac_copy_from_offset.src = replace_name(instr->instr.tac_copy_from_offset.src, var_map);
      instr->instr.tac_copy_from_offset.dst = replace_val(instr->instr.tac_copy_from_offset.dst, var_map);
      break;
    }
    case TACBOUNDARY: {
      // no change needed
      break;
    }
    case TACTRUNC: {
      instr->instr.tac_trunc.src = replace_val(instr->instr.tac_trunc.src, var_map);
      instr->instr.tac_trunc.dst = replace_val(instr->instr.tac_trunc.dst, var_map);
      break;
    }
    case TACEXTEND: {
      instr->instr.tac_extend.src = replace_val(instr->instr.tac_extend.src, var_map);
      instr->instr.tac_extend.dst = replace_val(instr->instr.tac_extend.dst, var_map);
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

  // create identifier maps so we can replace original identifiers with unique ones
  // inlined functions are small, small hashmap should work
  struct LabelMap* var_map = create_label_map(16);
  struct LabelMap* label_map = create_label_map(16);

  // generate unique parameter names for the callee
  struct Slice** params = arena_alloc(sizeof(struct Slice*) * callee->num_params);
  for (int i = 0; i < callee->num_params; i++) {
    params[i] = make_unique(callee->params[i], "inline");

    label_map_insert(var_map, callee->params[i], params[i]);
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
        ret_copy->instr.tac_copy.src =
            replace_val(callee_instr->instr.tac_return.src, var_map);
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

      replace_identifiers_and_labels(new_instr, var_map, label_map);

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
        prev = instr;
        continue; // callee not found in the call graph, skip inlining
      }
      if (!callee_node->consider_inlining) {
        prev = instr;
        continue; // recursion, tail call, or length exceeds limit
      }
      if (node->num_instrs > MAX_INLINE_CALLER_INSTRS) {
        prev = instr;
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
