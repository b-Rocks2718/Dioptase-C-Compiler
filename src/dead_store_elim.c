#include "dead_store_elim.h"
#include "slice.h"

#include <stdlib.h>

// Reverse a basic-block instruction list in place.
// The list is private to the block and ends at a NULL next pointer.
// Both head and last are updated, and a second call restores the original order.
static void reverse_block_body(struct TACInstrList* body) {
  if (body == NULL || body->head == NULL) {
    return;
  }

  struct TACInstr* prev = NULL;
  struct TACInstr* curr = body->head;
  body->last = body->head;
  while (curr != NULL) {
    struct TACInstr* next = curr->next;
    curr->next = prev;
    prev = curr;
    curr = next;
  }
  body->head = prev;
}

static bool instr_dead(struct TACInstr* instr) {
  switch (instr->type) {
    case TACRETURN:
    case TACCOND_JUMP:
    case TACJUMP: 
    case TACLABEL: 
    case TACBOUNDARY:
    case TACCALL:
    case TACCALL_INDIRECT: 
    case TACTAIL_CALL:
    case TACTAIL_CALL_INDIRECT:
    case TACSTORE: 
    case TACCOPY_TO_OFFSET:
    case TACVOLATILE_READ:
    case TACVOLATILE_WRITE:
    case TACVOLATILE_LOAD:
    case TACVOLATILE_STORE:
    case TACVOLATILE_COPY_TO_OFFSET:
    case TACVOLATILE_COPY_FROM_OFFSET: {
      // Control transfers, calls, stores, and volatile accesses are side effects.
      // A volatile read stays even when its destination is unused.
      return false;
    }
    case TACUNARY: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_unary.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACBINARY: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_binary.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACTRUNC: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_trunc.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACEXTEND: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_extend.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACCOPY_FROM_OFFSET: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_copy_from_offset.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACLOAD: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_load.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACCOPY: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_copy.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    case TACGET_ADDRESS: {
      // if dst not live, the instruction is dead
      struct Val* dst = instr->instr.tac_get_address.dst;
      if (dst != NULL && dst->val_type == VARIABLE) {
        return !slice_list_contains(instr->live_vars, dst->val.var_name);
      }
      return false; // conservatively assume it's not dead if we can't determine
    }
    default: {
      fprintf(stderr, "Unexpected TAC instruction type: %d\n", instr->type);
      exit(1);
    }
  }
}

static struct SliceList meet(struct CFGNode* node, struct SliceList static_vars) {
  struct SliceList live_vars = { NULL, NULL };
  
  // iterate over successors of the node
  for (struct CFGNodeEntry* succ = node->successors.head; succ != NULL; succ = succ->next) {
    switch (succ->node->type) {
      case CFG_ENTRY: {
        fprintf(stderr, "Unexpected CFG_ENTRY node in successors\n");
        exit(1);
      }
      case CFG_EXIT: {
        // add all static vars
        for (struct SliceListNode* node = static_vars.head; node != NULL; node = node->next) {
          slice_list_add(&live_vars, node->slice);
        }
        break;
      }
      case CFG_BASIC_BLOCK: {
        // add all live vars from the block
        for (struct SliceListNode* node = succ->node->live_vars.head; node != NULL; node = node->next) {
          if (!slice_list_contains(live_vars, node->slice)) {
            slice_list_add(&live_vars, node->slice);
          }
        }
        break;
      }
    }
  }
  return copy_slice_list(live_vars);
}

// Transfer function for live variable analysis.
// Updates the live variable set for a CFG node based on its end live variables.
// The block body is reversed for the backward walk and restored before return,
// so live sets are stored on the instructions elimination later inspects.
static void transfer(struct CFGNode* node, struct SliceList end_live_vars, 
    struct SliceList aliased_vars){
  struct SliceList current_live_vars = copy_slice_list(end_live_vars);
  reverse_block_body(&node->body);

  for (struct TACInstr* instr = node->body.head; instr != NULL; instr = instr->next) {
    instr->live_vars = copy_slice_list(current_live_vars);
    switch (instr->type) {
      case TACRETURN: {
        // generate return value if it exists and it's a variable
        if (instr->instr.tac_return.src != NULL && 
            instr->instr.tac_return.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_return.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_return.src->val.var_name);
        }
        break;
      }
      case TACBINARY: {
        // kill dst, generate src1 and src2

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_binary.dst->val.var_name);
        if (instr->instr.tac_binary.src1->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_binary.src1->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_binary.src1->val.var_name);
        }
        if (instr->instr.tac_binary.src2->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_binary.src2->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_binary.src2->val.var_name);
        }
        break;
      }
      case TACUNARY: {
        // kill dst, generate src

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_unary.dst->val.var_name);
        if (instr->instr.tac_unary.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_unary.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_unary.src->val.var_name);
        }
        break;
      }
      case TACCOND_JUMP: {
        // generate both sources
        if (instr->instr.tac_cond_jump.src1->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_cond_jump.src1->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_cond_jump.src1->val.var_name);
        }
        if (instr->instr.tac_cond_jump.src2->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_cond_jump.src2->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_cond_jump.src2->val.var_name);
        }
        break;
      }
      case TACJUMP: {
        // no effect on live variables
        break;
      }
      case TACLABEL: {
        // no effect on live variables
        break;
      }
      case TACCOPY:
      case TACVOLATILE_READ:
      case TACVOLATILE_WRITE: {
        // kill dst, generate src
        // A volatile write still overwrites dst for liveness, but instr_dead keeps the access.

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_copy.dst->val.var_name);
        if (instr->instr.tac_copy.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_copy.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_copy.src->val.var_name);
        }
        break;
      }
      case TACCALL: {
        // Void calls have no destination. Otherwise kill dst.
        // Generate every argument and every aliased variable, since the callee may read them.
        struct Val* dst = instr->instr.tac_call.dst;
        if (dst != NULL && dst->val_type == VARIABLE) {
          slice_list_remove(&current_live_vars, dst->val.var_name);
        }
        for (unsigned i = 0; i < instr->instr.tac_call.num_args; i++) {
          struct Val arg = instr->instr.tac_call.args[i];
          if (arg.val_type == VARIABLE && 
              !slice_list_contains(current_live_vars, arg.val.var_name)) {
            slice_list_add(&current_live_vars, arg.val.var_name);
          }
        }
        for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
          struct Slice* aliased_var = node->slice;
          if (!slice_list_contains(current_live_vars, aliased_var)) {
            slice_list_add(&current_live_vars, aliased_var);
          }
        }
        break;
      }
      case TACCALL_INDIRECT: {
        // Void indirect calls have no destination. Otherwise kill dst.
        // Generate the function pointer, every argument, and every aliased variable.
        struct Val* dst = instr->instr.tac_call_indirect.dst;
        if (dst != NULL && dst->val_type == VARIABLE) {
          slice_list_remove(&current_live_vars, dst->val.var_name);
        }
        if (instr->instr.tac_call_indirect.func->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_call_indirect.func->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_call_indirect.func->val.var_name);
        }
        for (unsigned i = 0; i < instr->instr.tac_call_indirect.num_args; i++) {
          struct Val arg = instr->instr.tac_call_indirect.args[i];
          if (arg.val_type == VARIABLE && 
              !slice_list_contains(current_live_vars, arg.val.var_name)) {
            slice_list_add(&current_live_vars, arg.val.var_name);
          }
        }
        for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
          struct Slice* aliased_var = node->slice;
          if (!slice_list_contains(current_live_vars, aliased_var)) {
            slice_list_add(&current_live_vars, aliased_var);
          }
        }
        break;
      }
      case TACTAIL_CALL: {
        // don't kill any variables, generate every argument and aliased variable.
        for (unsigned i = 0; i < instr->instr.tac_call.num_args; i++) {
          struct Val arg = instr->instr.tac_call.args[i];
          if (arg.val_type == VARIABLE && 
              !slice_list_contains(current_live_vars, arg.val.var_name)) {
            slice_list_add(&current_live_vars, arg.val.var_name);
          }
        }
        for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
          struct Slice* aliased_var = node->slice;
          if (!slice_list_contains(current_live_vars, aliased_var)) {
            slice_list_add(&current_live_vars, aliased_var);
          }
        }
        break;
      }
      case TACTAIL_CALL_INDIRECT: {
        // don't kill any variables, generate every argument, the function pointer, and every aliased variable.
        if (instr->instr.tac_call_indirect.func->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_call_indirect.func->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_call_indirect.func->val.var_name);
        }
        for (unsigned i = 0; i < instr->instr.tac_call_indirect.num_args; i++) {
          struct Val arg = instr->instr.tac_call_indirect.args[i];
          if (arg.val_type == VARIABLE && 
              !slice_list_contains(current_live_vars, arg.val.var_name)) {
            slice_list_add(&current_live_vars, arg.val.var_name);
          }
        }
        for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
          struct Slice* aliased_var = node->slice;
          if (!slice_list_contains(current_live_vars, aliased_var)) {
            slice_list_add(&current_live_vars, aliased_var);
          }
        }
        break;
      }
      case TACGET_ADDRESS: {
        // kill dst, ignore src
        slice_list_remove(&current_live_vars, instr->instr.tac_get_address.dst->val.var_name);
        break;
      }
      case TACVOLATILE_LOAD:
      case TACLOAD: {
        // kill dst, generate src_ptr, generate every aliased var
        slice_list_remove(&current_live_vars, instr->instr.tac_load.dst->val.var_name);
        if (instr->instr.tac_load.src_ptr->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_load.src_ptr->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_load.src_ptr->val.var_name);
        }
        for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
          struct Slice* aliased_var = node->slice;
          if (!slice_list_contains(current_live_vars, aliased_var)) {
            slice_list_add(&current_live_vars, aliased_var);
          }
        }
        break;
      }
      case TACVOLATILE_STORE:
      case TACSTORE: {
        // generate src and dst

        if (instr->instr.tac_store.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_store.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_store.src->val.var_name);
        }
        if (instr->instr.tac_store.dst_ptr->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_store.dst_ptr->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_store.dst_ptr->val.var_name);
        }
        break;
      }
      case TACVOLATILE_COPY_TO_OFFSET:
      case TACCOPY_TO_OFFSET: {
        // generate src, but do not kill dst
        if (instr->instr.tac_copy_to_offset.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_copy_to_offset.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_copy_to_offset.src->val.var_name);
        }
        break;
      }
      case TACVOLATILE_COPY_FROM_OFFSET:
      case TACCOPY_FROM_OFFSET: {
        // generate src, kill dst

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_copy_from_offset.dst->val.var_name);
        if (!slice_list_contains(current_live_vars, instr->instr.tac_copy_from_offset.src)) {
          slice_list_add(&current_live_vars, instr->instr.tac_copy_from_offset.src);
        }
        break;
      }
      case TACBOUNDARY: {
        // no effect on live variables
        break;
      }
      case TACTRUNC: {
        // kill dst, generate src

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_trunc.dst->val.var_name);
        if (instr->instr.tac_trunc.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_trunc.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_trunc.src->val.var_name);
        }
        break;
      }
      case TACEXTEND: {
        // kill dst, generate src

        // dst must be a var, or typechecking would have failed
        slice_list_remove(&current_live_vars, instr->instr.tac_extend.dst->val.var_name);
        if (instr->instr.tac_extend.src->val_type == VARIABLE && 
            !slice_list_contains(current_live_vars, instr->instr.tac_extend.src->val.var_name)) {
          slice_list_add(&current_live_vars, instr->instr.tac_extend.src->val.var_name);
        }
        break;
      }
    }
  }
  node->live_vars = copy_slice_list(current_live_vars);
  reverse_block_body(&node->body);
}

static void find_live_vars(struct CFG* cfg, struct SliceList static_vars, struct SliceList aliased_vars) {
  struct CFGNodeList worklist = {NULL, NULL};

  // iterate over all basic blocks
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    cfg_node_list_append(&worklist, cfg->nodes[i]);
    struct SliceList empty = {NULL, NULL};
    cfg->nodes[i]->live_vars = empty; // empty list is identity for union operator
  }

  while (!cfg_node_list_is_empty(&worklist)) {
    struct CFGNode* block = cfg_node_list_remove_front(&worklist);
    struct SliceList old_annotation = block->live_vars;
    struct SliceList incoming_live_vars = meet(block, static_vars);
    transfer(block, incoming_live_vars, aliased_vars);

    if (!compare_slice_lists(old_annotation, block->live_vars)) {
      // if the reaching copies have changed, add all predecessors to the worklist
      for (struct CFGNodeEntry* pred = block->predecessors.head; pred != NULL; pred = pred->next) {
        if (pred->node->type == CFG_ENTRY) {
          // do not add entry node to the worklist
          continue;
        }

        if (pred->node->type == CFG_EXIT) {
          // EXIT should not be the predecessor of any node
          fprintf(stderr, "Error: CFG_EXIT should not be the predecessor of any node.\n");
          exit(1);
        }

        if (!cfg_node_list_contains(&worklist, pred->node)) {
          cfg_node_list_append(&worklist, pred->node);
        }
      }
    }
  }

  // when the loop terminates, all instructions are annotated 
}

struct CFG* dead_store_elim(struct CFG* cfg, struct SliceList static_vars, struct SliceList aliased_vars){
  find_live_vars(cfg, static_vars, aliased_vars);

  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* block = cfg->nodes[i];
    struct TACInstr* prev_instr = NULL;
    for (struct TACInstr* instr = block->body.head; instr != NULL; instr = instr->next) {
      // remove dead instructions from the current basic block
      if (instr_dead(instr)) {
        if (prev_instr) {
          prev_instr->next = instr->next;
        } else {
          block->body.head = instr->next;
        }
        if (instr == block->body.last) {
          block->body.last = prev_instr;
        }
      } else {
        prev_instr = instr;
      }
    }
  }

  return cfg;
}
