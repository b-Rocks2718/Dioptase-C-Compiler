#include "copy_prop.h"
#include "cfg.h"
#include "arena.h"
#include "AST.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static bool compare_vals(const struct Val* a, const struct Val* b);

// Return true if list contains the copy dst = src.
static bool reaching_copy_list_contains(struct ReachingCopyList list, struct Val* dst, struct Val* src){
  for(struct ReachingCopy* curr = list.head; curr != NULL; curr = curr->next){
    if (compare_vals(curr->src, src) && compare_vals(curr->dst, dst)){
      return true;
    }
  }
  return false;
}

// Append dst = src to the reaching-copy list.
static void reaching_copy_list_add(struct ReachingCopyList* list, struct Val* dst, struct Val* src){
  struct ReachingCopy* new_copy = arena_alloc(sizeof(struct ReachingCopy));
  new_copy->dst = dst;
  new_copy->src = src;
  new_copy->next = NULL;
  if(list->last){
    list->last->next = new_copy;
  } else {
    list->head = new_copy;
  }
  list->last = new_copy;
}

// Unlink target from the list. prev is the predecessor of target, or NULL if target is head.
static void reaching_copy_list_remove(struct ReachingCopyList* list, struct ReachingCopy* target, struct ReachingCopy* prev){
  if (prev){
    prev->next = target->next;
  } else {
    list->head = target->next;
  }
  if (target == list->last){
    list->last = prev;
  }
}

static struct ReachingCopyList collect_all_copies(struct CFG* cfg){
  struct ReachingCopyList all_copies = {0};

  // for each basic block, for each instruction in each block,
  // collect all type-safe copies
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* block = cfg->nodes[i];
    for(struct TACInstr* instr = block->body.head; instr != NULL; instr = instr->next){
      if (instr->type != TACCOPY) {
        continue;
      }
      struct Val* src = instr->instr.tac_copy.src;
      struct Val* dst = instr->instr.tac_copy.dst;
      if (copy_is_type_safe(src, dst)) {
        reaching_copy_list_add(&all_copies, dst, src);
      }
    }
  }
  return all_copies;
}

// Create a deep copy of a ReachingCopyList.
static struct ReachingCopyList copy_reaching_copy_list(struct ReachingCopyList src){
  struct ReachingCopyList copy = {0};
  for(struct ReachingCopy* curr = src.head; curr != NULL; curr = curr->next){
    struct ReachingCopy* new_copy = arena_alloc(sizeof(struct ReachingCopy));
    *new_copy = *curr;
    new_copy->next = NULL;
    if(copy.last){
      copy.last->next = new_copy;
    } else {
      copy.head = new_copy;
    }
    copy.last = new_copy;
  }
  return copy;
}

// Return the variable name of val, or NULL if val is not a variable.
static struct Slice* val_name(const struct Val* val) {
  if (val == NULL || val->val_type != VARIABLE) {
    return NULL;
  }
  return val->val.var_name;
}

// Return true if a and b are the same TAC operand (name or constant bits).
static bool compare_vals(const struct Val* a, const struct Val* b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  if (a->val_type != b->val_type) {
    return false;
  }
  if (a->val_type == CONSTANT) {
    return a->val.const_value == b->val.const_value;
  }
  if (a->val.var_name == NULL || b->val.var_name == NULL) {
    return a->val.var_name == b->val.var_name;
  }
  return compare_slice_to_slice(a->val.var_name, b->val.var_name);
}

// Return true if type is plain char or signed char.
static bool is_char_or_schar(const struct Type* type) {
  return type != NULL && (type->type == CHAR_TYPE || type->type == SCHAR_TYPE);
}

// Return true if dst = src is safe to record for copy propagation.
bool copy_is_type_safe(const struct Val* src, const struct Val* dst) {
  if (src == NULL || dst == NULL) {
    return false;
  }
  if (src->val_type == CONSTANT && src->val.const_value == 0) {
    return true;
  }
  if (src->type == NULL || dst->type == NULL) {
    return false;
  }
  if (is_char_or_schar(src->type) && is_char_or_schar(dst->type)) {
    return true;
  }
  return compare_types(src->type, dst->type);
}

// Return true if copy's source or destination is the variable name.
static bool copy_involves_name(const struct ReachingCopy* copy, struct Slice* name) {
  if (name == NULL) {
    return false;
  }
  struct Slice* dst_name = val_name(copy->dst);
  struct Slice* src_name = val_name(copy->src);
  return (dst_name != NULL && compare_slice_to_slice(dst_name, name)) ||
         (src_name != NULL && compare_slice_to_slice(src_name, name));
}

// Return true if copy's source or destination is the same variable as val.
static bool copy_involves_val(const struct ReachingCopy* copy, const struct Val* val) {
  return copy_involves_name(copy, val_name(val));
}

// Return true if copy's source or destination is in the aliased-variable set.
static bool copy_involves_aliased(const struct ReachingCopy* copy, struct SliceList aliased) {
  struct Slice* dst_name = val_name(copy->dst);
  struct Slice* src_name = val_name(copy->src);
  return (dst_name != NULL && slice_list_contains(aliased, dst_name)) ||
         (src_name != NULL && slice_list_contains(aliased, src_name));
}

// Context for killing copies of a written variable.
struct KillValCtx {
  const struct Val* val;
};

// Context for killing copies of a written name (CopyToOffset destinations).
struct KillNameCtx {
  struct Slice* name;
};

// Context for call/store kills: aliased names plus an optional written dest.
struct KillCallCtx {
  struct SliceList aliased;
  const struct Val* dest;
};

// Predicate: copy mentions the written variable in ctx.
static bool pred_involves_val(const struct ReachingCopy* copy, const void* ctx) {
  const struct KillValCtx* kill = ctx;
  return copy_involves_val(copy, kill->val);
}

// Predicate: copy mentions the written name in ctx.
static bool pred_involves_name(const struct ReachingCopy* copy, const void* ctx) {
  const struct KillNameCtx* kill = ctx;
  return copy_involves_name(copy, kill->name);
}

// Predicate: copy mentions an aliased variable.
static bool pred_involves_aliased(const struct ReachingCopy* copy, const void* ctx) {
  const struct SliceList* aliased = ctx;
  return copy_involves_aliased(copy, *aliased);
}

// Predicate: copy is killed by a call (aliased names or the call dest).
static bool pred_call_kill(const struct ReachingCopy* copy, const void* ctx) {
  const struct KillCallCtx* kill = ctx;
  return copy_involves_aliased(copy, kill->aliased) || copy_involves_val(copy, kill->dest);
}

// Remove every copy for which should_kill returns true.
static void kill_copies(struct ReachingCopyList* list,
                        bool (*should_kill)(const struct ReachingCopy*, const void*),
                        const void* ctx) {
  struct ReachingCopy* prev = NULL;
  struct ReachingCopy* curr = list->head;
  while (curr != NULL) {
    struct ReachingCopy* next = curr->next;
    if (should_kill(curr, ctx)) {
      reaching_copy_list_remove(list, curr, prev);
    } else {
      prev = curr;
    }
    curr = next;
  }
}

// Remove copies whose source or destination is val.
static void kill_copies_involving_val(struct ReachingCopyList* list, const struct Val* val) {
  struct KillValCtx ctx = { val };
  kill_copies(list, pred_involves_val, &ctx);
}

// Remove copies whose source or destination is name.
static void kill_copies_involving_name(struct ReachingCopyList* list, struct Slice* name) {
  struct KillNameCtx ctx = { name };
  kill_copies(list, pred_involves_name, &ctx);
}

// Remove copies that mention any aliased variable.
static void kill_copies_involving_aliased(struct ReachingCopyList* list, struct SliceList aliased) {
  kill_copies(list, pred_involves_aliased, &aliased);
}

// Remove copies killed by a call: aliased variables and the optional dest.
static void kill_copies_for_call(struct ReachingCopyList* list,
                                 struct SliceList aliased,
                                 const struct Val* dest) {
  struct KillCallCtx ctx = { aliased, dest };
  kill_copies(list, pred_call_kill, &ctx);
}

// Given the copies reaching a block, determine which
// copies reach each instruction, and which copies reach the end of the block.
static void transfer(struct CFGNode* block,
    struct ReachingCopyList reaching_copies,
    struct SliceList aliased_vars){
  struct ReachingCopyList current_reaching_copies = copy_reaching_copy_list(reaching_copies);

  for (struct TACInstr* instr = block->body.head; instr != NULL; instr = instr->next){
    instr->reaching_copies = copy_reaching_copy_list(current_reaching_copies);

    switch(instr->type){
      case TACCOPY: {
        struct Val* dst = instr->instr.tac_copy.dst;
        struct Val* src = instr->instr.tac_copy.src;
        // y = x is a no-op if x = y already reaches, so it must not kill x = y.
        if (reaching_copy_list_contains(current_reaching_copies, src, dst)){
          continue;
        }

        kill_copies_involving_val(&current_reaching_copies, dst);
        if (copy_is_type_safe(src, dst)) {
          reaching_copy_list_add(&current_reaching_copies, dst, src);
        }
        break;
      }
      case TACCALL:
        kill_copies_for_call(&current_reaching_copies, aliased_vars, instr->instr.tac_call.dst);
        break;
      case TACCALL_INDIRECT:
        kill_copies_for_call(&current_reaching_copies, aliased_vars, instr->instr.tac_call_indirect.dst);
        break;
      case TACUNARY:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_unary.dst);
        break;
      case TACBINARY:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_binary.dst);
        break;
      case TACSTORE:
        kill_copies_involving_aliased(&current_reaching_copies, aliased_vars);
        break;
      case TACTRUNC:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_trunc.dst);
        break;
      case TACEXTEND:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_extend.dst);
        break;
      case TACLOAD:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_load.dst);
        break;
      case TACGET_ADDRESS:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_get_address.dst);
        break;
      case TACCOPY_TO_OFFSET:
        kill_copies_involving_name(&current_reaching_copies, instr->instr.tac_copy_to_offset.dst);
        break;
      case TACCOPY_FROM_OFFSET:
        kill_copies_involving_val(&current_reaching_copies, instr->instr.tac_copy_from_offset.dst);
        break;
      case TACRETURN:
      case TACCOND_JUMP:
      case TACJUMP:
      case TACLABEL:
      case TACBOUNDARY:
        break;
    }
  }

  block->reaching_copies = copy_reaching_copy_list(current_reaching_copies);
}

// Return true when both lists contain the same copies. Order and duplicate entries are ignored.
bool compare_reaching_copy_lists(struct ReachingCopyList list1, struct ReachingCopyList list2) {
  for (struct ReachingCopy* copy = list1.head; copy != NULL; copy = copy->next) {
    if (!reaching_copy_list_contains(list2, copy->dst, copy->src)) {
      return false;
    }
  }
  for (struct ReachingCopy* copy = list2.head; copy != NULL; copy = copy->next) {
    if (!reaching_copy_list_contains(list1, copy->dst, copy->src)) {
      return false;
    }
  }
  return true;
}

// Intersect two reaching copy lists, 
// returning a new list containing only copies present in both lists.
struct ReachingCopyList intersect_reaching_copy_lists(struct ReachingCopyList list1, struct ReachingCopyList list2){
  struct ReachingCopyList result = copy_reaching_copy_list(list1);
  struct ReachingCopy* prev = NULL;
  for (struct ReachingCopy* copy = result.head; copy != NULL; copy = copy->next) {
    if (!reaching_copy_list_contains(list2, copy->dst, copy->src)) {
      reaching_copy_list_remove(&result, copy, prev);
      continue;
    }
    prev = copy;
  }
  return result;
}

// Compute intersection of reaching copy lists of all the predecessors of a block
struct ReachingCopyList meet(struct CFGNode* block, struct ReachingCopyList all_copies){
  struct ReachingCopyList incoming_copies = copy_reaching_copy_list(all_copies);

  // take intersection of incoming copies from all predecessors
  for (struct CFGNodeEntry* pred = block->predecessors.head; pred != NULL; pred = pred->next) {
    if (pred->node->type == CFG_ENTRY){
      // node that follows the entry node has no meaningful incoming copies
      struct ReachingCopyList empty_list = {NULL, NULL};
      return empty_list;
    }
    if (pred->node->type == CFG_EXIT) {
      // node that follows exit is a compiler bug
      fprintf(stderr, "Error: node following CFG_EXIT is invalid.\n");
      exit(1);
    }
    // node must be a basic block
    struct ReachingCopyList pred_copies = pred->node->reaching_copies;
    incoming_copies = intersect_reaching_copy_lists(incoming_copies, pred_copies);
  }
  return incoming_copies;
}

// Find reaching copies for all blocks in the CFG.
void find_reaching_copies(struct CFG* cfg, struct SliceList aliased_vars){
  struct ReachingCopyList all_copies = collect_all_copies(cfg);

  struct CFGNodeList worklist = {NULL, NULL};

  // iterate over all basic blocks
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    cfg_node_list_append(&worklist, cfg->nodes[i]);
    cfg->nodes[i]->reaching_copies = copy_reaching_copy_list(all_copies);
  }

  while (!cfg_node_list_is_empty(&worklist)) {
    struct CFGNode* block = cfg_node_list_remove_front(&worklist);
    struct ReachingCopyList old_annotation = block->reaching_copies;
    struct ReachingCopyList incoming_copies = meet(block, all_copies);
    transfer(block, incoming_copies, aliased_vars);

    if (!compare_reaching_copy_lists(old_annotation, block->reaching_copies)) {
      // if the reaching copies have changed, add all successors to the worklist
      for (struct CFGNodeEntry* succ = block->successors.head; succ != NULL; succ = succ->next) {
        if (succ->node->type == CFG_EXIT) {
          // do not add exit node to the worklist
          continue;
        }

        if (succ->node->type == CFG_ENTRY) {
          // ENTRY should not be the successor of any node
          fprintf(stderr, "Error: CFG_ENTRY should not be the successor of any node.\n");
          exit(1);
        }

        if (!cfg_node_list_contains(&worklist, succ->node)) {
          cfg_node_list_append(&worklist, succ->node);
        }
      }
    }
  }

  // when the loop terminates, all instructions are annotated 
}

// Replace the operand with its reaching copy if one exists
static struct Val* replace_operand(struct Val* operand, struct ReachingCopyList reaching_copies) {
  if (operand == NULL) {
    return NULL;
  }

  if (operand->val_type == CONSTANT) {
    return operand;
  }

  for (struct ReachingCopy* copy = reaching_copies.head; copy != NULL; copy = copy->next) {
    if (compare_vals(copy->dst, operand)) {
      return copy->src;
    }
  }
  return operand;
}

// Rewrite the instruction by replacing its operands with their reaching copies if they exist.
// Returns true if the instruction can be deleted (is redundant), false otherwise.
static bool rewrite_instr(struct TACInstr* instr){
  struct ReachingCopyList reaching_copies = instr->reaching_copies;
  switch (instr->type) {
    case TACCOPY: {
      for (struct ReachingCopy* copy = reaching_copies.head; copy != NULL; copy = copy->next) {
        if ((compare_vals(copy->dst, instr->instr.tac_copy.dst) &&
             compare_vals(copy->src, instr->instr.tac_copy.src)) ||
            (compare_vals(copy->dst, instr->instr.tac_copy.src) &&
             compare_vals(copy->src, instr->instr.tac_copy.dst))) {
          // this is a redundant copy instruction, it can be deleted
          return true;
        }
      }
      instr->instr.tac_copy.src = replace_operand(instr->instr.tac_copy.src, reaching_copies);
      break;
    }
    case TACRETURN: {
      instr->instr.tac_return.src = replace_operand(instr->instr.tac_return.src, reaching_copies);
      break;
    }
    case TACUNARY: {
      instr->instr.tac_unary.src = replace_operand(instr->instr.tac_unary.src, reaching_copies);
      break;
    }
    case TACBINARY: {
      instr->instr.tac_binary.src1 = replace_operand(instr->instr.tac_binary.src1, reaching_copies);
      instr->instr.tac_binary.src2 = replace_operand(instr->instr.tac_binary.src2, reaching_copies);
      break;
    }
    case TACCOND_JUMP: {
      instr->instr.tac_cond_jump.src1 = replace_operand(instr->instr.tac_cond_jump.src1, reaching_copies);
      instr->instr.tac_cond_jump.src2 = replace_operand(instr->instr.tac_cond_jump.src2, reaching_copies);
      break;
    }
    case TACJUMP: {
      // no operands to replace for an unconditional jump
      break;
    }
    case TACLABEL: {
      // no operands to replace for a label
      break;
    }
    case TACLOAD: {
      instr->instr.tac_load.src_ptr = replace_operand(instr->instr.tac_load.src_ptr, reaching_copies);
      break;
    }
    case TACSTORE: {
      instr->instr.tac_store.src = replace_operand(instr->instr.tac_store.src, reaching_copies);
      break;
    }
    case TACTRUNC: {
      instr->instr.tac_trunc.src = replace_operand(instr->instr.tac_trunc.src, reaching_copies);
      break;
    }
    case TACEXTEND: {
      instr->instr.tac_extend.src = replace_operand(instr->instr.tac_extend.src, reaching_copies);
      break;
    }
    case TACGET_ADDRESS: {
      // can't use copy propagation for get_address
      break;
    }
    case TACBOUNDARY: {
      // no operands to replace for a boundary instruction
      break;
    }
    case TACCOPY_TO_OFFSET: {
      instr->instr.tac_copy_to_offset.src = replace_operand(instr->instr.tac_copy_to_offset.src, reaching_copies);
      break;
    }
    case TACCOPY_FROM_OFFSET: {
      // can't use copy propagation for copy_from_offset
      break;
    }
    case TACCALL: {
      for (unsigned i = 0; i < instr->instr.tac_call.num_args; i++) {
        instr->instr.tac_call.args[i] = *replace_operand(&instr->instr.tac_call.args[i], reaching_copies);
      }
      break;
    }
    case TACCALL_INDIRECT: {
      for (unsigned i = 0; i < instr->instr.tac_call_indirect.num_args; i++) {
        instr->instr.tac_call_indirect.args[i] = *replace_operand(&instr->instr.tac_call_indirect.args[i], reaching_copies);
      }
      break;
    }
  }
  return false;
}

// Perform copy propagation on the given CFG using the reaching copy information.
struct CFG* copy_prop(struct CFG* cfg, struct SliceList aliased_vars){
  find_reaching_copies(cfg, aliased_vars);

  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* block = cfg->nodes[i];
    struct TACInstr* prev_instr = NULL;
    for (struct TACInstr* instr = block->body.head; instr != NULL; instr = instr->next) {
      if (rewrite_instr(instr)) {
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
