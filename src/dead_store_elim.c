#include "dead_store_elim.h"
#include "slice.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Configure the pass-local hash table and the machine-independent width of a
// liveness word. These constants keep growth policy and bit arithmetic named.
enum {
  kInitialVariableBuckets = 256,
  kVariableMapLoadNumerator = 3,
  kVariableMapLoadDenominator = 4,
  kLiveWordBits = 64,
};

// Link one borrowed variable name to its dense liveness-bit index. Entries are
// owned by VariableIndex and chained within one hash bucket.
struct VariableIndexEntry {
  struct Slice* name;
  size_t index;
  struct VariableIndexEntry* next;
};

// Map variable names to consecutive bit indices for one DSE invocation. The
// table owns its buckets and entries, but the Slice names remain arena-owned.
struct VariableIndex {
  struct VariableIndexEntry** buckets;
  size_t bucket_count;
  size_t count;
};

// View a fixed-width bitset of live variables. Some views own separately
// allocated words, while block views refer into the shared block-live array.
struct LiveSet {
  uint64_t* words;
  size_t word_count;
};

// Hold a bounded FIFO of CFG-node indices. queued has one flag per CFG node so
// a node can appear at most once while waiting to be processed.
struct BlockQueue {
  unsigned* items;
  bool* queued;
  size_t capacity;
  size_t head;
  size_t count;
};

// Allocate zeroed pass-local storage or terminate with a contextual diagnostic.
// The caller owns the returned allocation and releases it with free.
static void* dse_calloc(size_t count, size_t size, const char* purpose) {
  if (count != 0 && size > SIZE_MAX / count) {
    fprintf(stderr,
            "Dead-store elimination error: allocation size overflow while %s\n",
            purpose);
    exit(1);
  }
  void* allocation = calloc(count, size);
  if (allocation == NULL && count != 0 && size != 0) {
    fprintf(stderr,
            "Dead-store elimination error: unable to allocate %zu bytes while %s\n",
            count * size, purpose);
    exit(1);
  }
  return allocation;
}

// Initialize an empty variable index with the starting bucket count.
static void variable_index_init(struct VariableIndex* index) {
  index->bucket_count = kInitialVariableBuckets;
  index->count = 0;
  index->buckets = dse_calloc(index->bucket_count, sizeof(*index->buckets),
                              "creating the variable index");
}

// Release every entry and bucket owned by index and reset it to an empty state.
static void variable_index_destroy(struct VariableIndex* index) {
  for (size_t bucket = 0; bucket < index->bucket_count; ++bucket) {
    struct VariableIndexEntry* entry = index->buckets[bucket];
    while (entry != NULL) {
      struct VariableIndexEntry* next = entry->next;
      free(entry);
      entry = next;
    }
  }
  free(index->buckets);
  index->buckets = NULL;
  index->bucket_count = 0;
  index->count = 0;
}

// Find the entry for name, returning a borrowed pointer or NULL when absent.
static struct VariableIndexEntry* variable_index_find_entry(
    const struct VariableIndex* index, const struct Slice* name) {
  if (name == NULL) {
    return NULL;
  }
  size_t bucket = hash_slice(name) % index->bucket_count;
  for (struct VariableIndexEntry* entry = index->buckets[bucket];
       entry != NULL;
       entry = entry->next) {
    if (compare_slice_to_slice(entry->name, name)) {
      return entry;
    }
  }
  return NULL;
}

// Double the bucket array and relink existing entries without changing indices.
static void variable_index_grow(struct VariableIndex* index) {
  size_t new_bucket_count = index->bucket_count * 2;
  if (new_bucket_count < index->bucket_count) {
    fprintf(stderr,
            "Dead-store elimination error: variable index bucket count overflow\n");
    exit(1);
  }

  struct VariableIndexEntry** new_buckets =
      dse_calloc(new_bucket_count, sizeof(*new_buckets),
                 "growing the variable index");
  for (size_t bucket = 0; bucket < index->bucket_count; ++bucket) {
    struct VariableIndexEntry* entry = index->buckets[bucket];
    while (entry != NULL) {
      struct VariableIndexEntry* next = entry->next;
      size_t new_bucket = hash_slice(entry->name) % new_bucket_count;
      entry->next = new_buckets[new_bucket];
      new_buckets[new_bucket] = entry;
      entry = next;
    }
  }

  free(index->buckets);
  index->buckets = new_buckets;
  index->bucket_count = new_bucket_count;
}

// Return name's existing bit index, or insert it and assign the next index.
// A NULL name is ignored and represented by SIZE_MAX.
static size_t variable_index_add(struct VariableIndex* index, struct Slice* name) {
  if (name == NULL) {
    return SIZE_MAX;
  }

  struct VariableIndexEntry* existing = variable_index_find_entry(index, name);
  if (existing != NULL) {
    return existing->index;
  }

  if (index->count >=
      index->bucket_count * kVariableMapLoadNumerator /
          kVariableMapLoadDenominator) {
    variable_index_grow(index);
  }

  struct VariableIndexEntry* entry =
      dse_calloc(1, sizeof(*entry), "adding a liveness variable");
  entry->name = name;
  entry->index = index->count++;
  size_t bucket = hash_slice(name) % index->bucket_count;
  entry->next = index->buckets[bucket];
  index->buckets[bucket] = entry;
  return entry->index;
}

// Return the previously assigned index for name. Absence is an internal pass
// error because every operand must be collected before bitsets are allocated.
static size_t variable_index_get(const struct VariableIndex* index,
                                 const struct Slice* name) {
  struct VariableIndexEntry* entry = variable_index_find_entry(index, name);
  if (entry == NULL) {
    fprintf(stderr,
            "Dead-store elimination error: liveness variable '%.*s' was not indexed\n",
            name == NULL ? 0 : (int)name->len,
            name == NULL ? "" : name->start);
    exit(1);
  }
  return entry->index;
}

// Add val to the index when it is a variable operand.
static void variable_index_add_val(struct VariableIndex* index, struct Val* val) {
  if (val != NULL && val->val_type == VARIABLE) {
    variable_index_add(index, val->val.var_name);
  }
}

// Add every variable operand in an argument array to the index.
static void variable_index_add_args(struct VariableIndex* index,
                                    struct Val* args,
                                    size_t num_args) {
  for (size_t i = 0; i < num_args; ++i) {
    variable_index_add_val(index, &args[i]);
  }
}

// Add all variable operands read or written by one TAC instruction.
static void collect_instruction_variables(struct VariableIndex* index,
                                          struct TACInstr* instr) {
  switch (instr->type) {
    case TACRETURN:
      variable_index_add_val(index, instr->instr.tac_return.src);
      break;
    case TACUNARY:
      variable_index_add_val(index, instr->instr.tac_unary.dst);
      variable_index_add_val(index, instr->instr.tac_unary.src);
      break;
    case TACBINARY:
      variable_index_add_val(index, instr->instr.tac_binary.dst);
      variable_index_add_val(index, instr->instr.tac_binary.src1);
      variable_index_add_val(index, instr->instr.tac_binary.src2);
      break;
    case TACCOND_JUMP:
      variable_index_add_val(index, instr->instr.tac_cond_jump.src1);
      variable_index_add_val(index, instr->instr.tac_cond_jump.src2);
      break;
    case TACCOPY:
    case TACVOLATILE_READ:
    case TACVOLATILE_WRITE:
      variable_index_add_val(index, instr->instr.tac_copy.dst);
      variable_index_add_val(index, instr->instr.tac_copy.src);
      break;
    case TACCALL:
      variable_index_add_val(index, instr->instr.tac_call.dst);
      variable_index_add_args(index, instr->instr.tac_call.args,
                              instr->instr.tac_call.num_args);
      break;
    case TACCALL_INDIRECT:
      variable_index_add_val(index, instr->instr.tac_call_indirect.func);
      variable_index_add_val(index, instr->instr.tac_call_indirect.dst);
      variable_index_add_args(index, instr->instr.tac_call_indirect.args,
                              instr->instr.tac_call_indirect.num_args);
      break;
    case TACTAIL_CALL:
      variable_index_add_args(index, instr->instr.tac_tail_call.args,
                              instr->instr.tac_tail_call.num_args);
      break;
    case TACTAIL_CALL_INDIRECT:
      variable_index_add_val(index, instr->instr.tac_tail_call_indirect.func);
      variable_index_add_args(index, instr->instr.tac_tail_call_indirect.args,
                              instr->instr.tac_tail_call_indirect.num_args);
      break;
    case TACGET_ADDRESS:
      variable_index_add_val(index, instr->instr.tac_get_address.dst);
      variable_index_add_val(index, instr->instr.tac_get_address.src);
      break;
    case TACLOAD:
    case TACVOLATILE_LOAD:
      variable_index_add_val(index, instr->instr.tac_load.dst);
      variable_index_add_val(index, instr->instr.tac_load.src_ptr);
      break;
    case TACSTORE:
    case TACVOLATILE_STORE:
      variable_index_add_val(index, instr->instr.tac_store.dst_ptr);
      variable_index_add_val(index, instr->instr.tac_store.src);
      break;
    case TACCOPY_TO_OFFSET:
    case TACVOLATILE_COPY_TO_OFFSET:
      variable_index_add(index, instr->instr.tac_copy_to_offset.dst);
      variable_index_add_val(index, instr->instr.tac_copy_to_offset.src);
      break;
    case TACCOPY_FROM_OFFSET:
    case TACVOLATILE_COPY_FROM_OFFSET:
      variable_index_add_val(index, instr->instr.tac_copy_from_offset.dst);
      variable_index_add(index, instr->instr.tac_copy_from_offset.src);
      break;
    case TACTRUNC:
      variable_index_add_val(index, instr->instr.tac_trunc.dst);
      variable_index_add_val(index, instr->instr.tac_trunc.src);
      break;
    case TACEXTEND:
      variable_index_add_val(index, instr->instr.tac_extend.dst);
      variable_index_add_val(index, instr->instr.tac_extend.src);
      break;
    case TACJUMP:
    case TACLABEL:
    case TACBOUNDARY:
      break;
  }
}

// Build the complete variable universe before allocating any liveness bitset.
// Static and aliased names are included even when not present in the CFG body.
static void collect_cfg_variables(struct VariableIndex* index,
                                  struct CFG* cfg,
                                  struct SliceList static_vars,
                                  struct SliceList aliased_vars) {
  for (struct SliceListNode* node = static_vars.head; node != NULL; node = node->next) {
    variable_index_add(index, node->slice);
  }
  for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
    variable_index_add(index, node->slice);
  }
  for (unsigned i = 1; i + 1 < cfg->num_nodes; ++i) {
    for (struct TACInstr* instr = cfg->nodes[i]->body.head;
         instr != NULL;
         instr = instr->next) {
      collect_instruction_variables(index, instr);
    }
  }
}

// Allocate an owning, zero-initialized liveness set of word_count words.
static struct LiveSet live_set_allocate(size_t word_count, const char* purpose) {
  struct LiveSet set;
  set.word_count = word_count;
  set.words = dse_calloc(word_count, sizeof(*set.words), purpose);
  return set;
}

// Remove every variable from set without changing its storage.
static void live_set_clear(struct LiveSet set) {
  memset(set.words, 0, set.word_count * sizeof(*set.words));
}

// Replace dst with src. Both sets cover the same variable universe.
static void live_set_copy(struct LiveSet dst, struct LiveSet src) {
  memcpy(dst.words, src.words, dst.word_count * sizeof(*dst.words));
}

// Add every variable in src to dst using a word-wise union.
static void live_set_union(struct LiveSet dst, struct LiveSet src) {
  for (size_t i = 0; i < dst.word_count; ++i) {
    dst.words[i] |= src.words[i];
  }
}

// Return true when two sets contain exactly the same variables.
static bool live_set_equal(struct LiveSet a, struct LiveSet b) {
  return memcmp(a.words, b.words, a.word_count * sizeof(*a.words)) == 0;
}

// Return whether the variable identified by name is present in set.
static bool live_set_contains(struct LiveSet set,
                              const struct VariableIndex* index,
                              const struct Slice* name) {
  size_t variable = variable_index_get(index, name);
  return (set.words[variable / kLiveWordBits] &
          (UINT64_C(1) << (variable % kLiveWordBits))) != 0;
}

// Mark the variable identified by name as live in set.
static void live_set_add(struct LiveSet set,
                         const struct VariableIndex* index,
                         const struct Slice* name) {
  size_t variable = variable_index_get(index, name);
  set.words[variable / kLiveWordBits] |=
      UINT64_C(1) << (variable % kLiveWordBits);
}

// Mark the variable identified by name as not live in set.
static void live_set_remove(struct LiveSet set,
                            const struct VariableIndex* index,
                            const struct Slice* name) {
  size_t variable = variable_index_get(index, name);
  set.words[variable / kLiveWordBits] &=
      ~(UINT64_C(1) << (variable % kLiveWordBits));
}

// Add val to set when it is a variable operand.
static void live_set_add_val(struct LiveSet set,
                             const struct VariableIndex* index,
                             struct Val* val) {
  if (val != NULL && val->val_type == VARIABLE) {
    live_set_add(set, index, val->val.var_name);
  }
}

// Remove val from set when it is a variable operand.
static void live_set_remove_val(struct LiveSet set,
                                const struct VariableIndex* index,
                                struct Val* val) {
  if (val != NULL && val->val_type == VARIABLE) {
    live_set_remove(set, index, val->val.var_name);
  }
}

// Add every variable operand in an argument array to set.
static void live_set_add_args(struct LiveSet set,
                              const struct VariableIndex* index,
                              struct Val* args,
                              size_t num_args) {
  for (size_t i = 0; i < num_args; ++i) {
    live_set_add_val(set, index, &args[i]);
  }
}

// Convert a SliceList into bits in an already allocated liveness set.
static void live_set_add_slice_list(struct LiveSet set,
                                    const struct VariableIndex* index,
                                    struct SliceList list) {
  for (struct SliceListNode* node = list.head; node != NULL; node = node->next) {
    live_set_add(set, index, node->slice);
  }
}

// Return true when destination is a variable whose current value is not live.
static bool destination_is_dead(struct LiveSet live,
                                const struct VariableIndex* index,
                                struct Val* destination) {
  return destination != NULL && destination->val_type == VARIABLE &&
         !live_set_contains(live, index, destination->val.var_name);
}

// Test an instruction against the variables live immediately after it.
static bool instruction_is_dead(struct TACInstr* instr,
                                struct LiveSet live,
                                const struct VariableIndex* index) {
  switch (instr->type) {
    case TACUNARY:
      return destination_is_dead(live, index, instr->instr.tac_unary.dst);
    case TACBINARY:
      return destination_is_dead(live, index, instr->instr.tac_binary.dst);
    case TACTRUNC:
      return destination_is_dead(live, index, instr->instr.tac_trunc.dst);
    case TACEXTEND:
      return destination_is_dead(live, index, instr->instr.tac_extend.dst);
    case TACCOPY_FROM_OFFSET:
      return destination_is_dead(live, index,
                                 instr->instr.tac_copy_from_offset.dst);
    case TACLOAD:
      return destination_is_dead(live, index, instr->instr.tac_load.dst);
    case TACCOPY:
      return destination_is_dead(live, index, instr->instr.tac_copy.dst);
    case TACGET_ADDRESS:
      return destination_is_dead(live, index,
                                 instr->instr.tac_get_address.dst);
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
    case TACVOLATILE_COPY_FROM_OFFSET:
      // Control transfers, calls, stores, and volatile accesses are observable.
      return false;
  }

  fprintf(stderr,
          "Dead-store elimination error: unsupported TAC instruction type %d "
          "while testing liveness\n",
          instr->type);
  exit(1);
}

// Apply the backward liveness transfer function for one instruction.
static void transfer_instruction(struct TACInstr* instr,
                                 struct LiveSet live,
                                 struct LiveSet aliased,
                                 const struct VariableIndex* index) {
  switch (instr->type) {
    case TACRETURN:
      live_set_add_val(live, index, instr->instr.tac_return.src);
      break;
    case TACBINARY:
      live_set_remove_val(live, index, instr->instr.tac_binary.dst);
      live_set_add_val(live, index, instr->instr.tac_binary.src1);
      live_set_add_val(live, index, instr->instr.tac_binary.src2);
      break;
    case TACUNARY:
      live_set_remove_val(live, index, instr->instr.tac_unary.dst);
      live_set_add_val(live, index, instr->instr.tac_unary.src);
      break;
    case TACCOND_JUMP:
      live_set_add_val(live, index, instr->instr.tac_cond_jump.src1);
      live_set_add_val(live, index, instr->instr.tac_cond_jump.src2);
      break;
    case TACCOPY:
    case TACVOLATILE_READ:
    case TACVOLATILE_WRITE:
      live_set_remove_val(live, index, instr->instr.tac_copy.dst);
      live_set_add_val(live, index, instr->instr.tac_copy.src);
      break;
    case TACCALL:
      live_set_remove_val(live, index, instr->instr.tac_call.dst);
      live_set_add_args(live, index, instr->instr.tac_call.args,
                        instr->instr.tac_call.num_args);
      live_set_union(live, aliased);
      break;
    case TACCALL_INDIRECT:
      live_set_remove_val(live, index, instr->instr.tac_call_indirect.dst);
      live_set_add_val(live, index, instr->instr.tac_call_indirect.func);
      live_set_add_args(live, index, instr->instr.tac_call_indirect.args,
                        instr->instr.tac_call_indirect.num_args);
      live_set_union(live, aliased);
      break;
    case TACTAIL_CALL:
      live_set_add_args(live, index, instr->instr.tac_tail_call.args,
                        instr->instr.tac_tail_call.num_args);
      live_set_union(live, aliased);
      break;
    case TACTAIL_CALL_INDIRECT:
      live_set_add_val(live, index, instr->instr.tac_tail_call_indirect.func);
      live_set_add_args(live, index, instr->instr.tac_tail_call_indirect.args,
                        instr->instr.tac_tail_call_indirect.num_args);
      live_set_union(live, aliased);
      break;
    case TACGET_ADDRESS:
      live_set_remove_val(live, index, instr->instr.tac_get_address.dst);
      break;
    case TACLOAD:
    case TACVOLATILE_LOAD:
      live_set_remove_val(live, index, instr->instr.tac_load.dst);
      live_set_add_val(live, index, instr->instr.tac_load.src_ptr);
      live_set_union(live, aliased);
      break;
    case TACSTORE:
    case TACVOLATILE_STORE:
      live_set_add_val(live, index, instr->instr.tac_store.src);
      live_set_add_val(live, index, instr->instr.tac_store.dst_ptr);
      break;
    case TACCOPY_TO_OFFSET:
    case TACVOLATILE_COPY_TO_OFFSET:
      live_set_add_val(live, index, instr->instr.tac_copy_to_offset.src);
      break;
    case TACCOPY_FROM_OFFSET:
    case TACVOLATILE_COPY_FROM_OFFSET:
      live_set_remove_val(live, index, instr->instr.tac_copy_from_offset.dst);
      live_set_add(live, index, instr->instr.tac_copy_from_offset.src);
      break;
    case TACTRUNC:
      live_set_remove_val(live, index, instr->instr.tac_trunc.dst);
      live_set_add_val(live, index, instr->instr.tac_trunc.src);
      break;
    case TACEXTEND:
      live_set_remove_val(live, index, instr->instr.tac_extend.dst);
      live_set_add_val(live, index, instr->instr.tac_extend.src);
      break;
    case TACJUMP:
    case TACLABEL:
    case TACBOUNDARY:
      break;
  }
}

// Reverse a private basic-block instruction list in place. A second call
// restores its original order.
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

// Return node's stable position in cfg->nodes. CFG edges store pointers, while
// pass-local arrays are indexed by this position.
static unsigned cfg_node_index(const struct CFG* cfg, const struct CFGNode* node) {
  for (unsigned i = 0; i < cfg->num_nodes; ++i) {
    if (cfg->nodes[i] == node) {
      return i;
    }
  }
  fprintf(stderr,
          "Dead-store elimination error: CFG edge references a node outside "
          "the current graph\n");
  exit(1);
}

// Return a non-owning LiveSet view for one block in the contiguous live array.
static struct LiveSet block_live_set(uint64_t* block_live_words,
                                     size_t word_count,
                                     unsigned block_index) {
  struct LiveSet set;
  set.words = block_live_words + (size_t)block_index * word_count;
  set.word_count = word_count;
  return set;
}

// Compute the variables live immediately after node from successor live-in
// sets. Reaching the synthetic exit keeps static variables observable.
static void meet_successors(const struct CFG* cfg,
                            const struct CFGNode* node,
                            uint64_t* block_live_words,
                            struct LiveSet static_vars,
                            struct LiveSet result) {
  live_set_clear(result);
  for (struct CFGNodeEntry* successor = node->successors.head;
       successor != NULL;
       successor = successor->next) {
    switch (successor->node->type) {
      case CFG_ENTRY:
        fprintf(stderr,
                "Dead-store elimination error: CFG entry node appears as a "
                "successor\n");
        exit(1);
      case CFG_EXIT:
        live_set_union(result, static_vars);
        break;
      case CFG_BASIC_BLOCK: {
        unsigned successor_index = cfg_node_index(cfg, successor->node);
        live_set_union(
            result,
            block_live_set(block_live_words, result.word_count, successor_index));
        break;
      }
    }
  }
}

// Apply every instruction's backward transfer function to live. The block is
// restored to its original forward order before returning.
static void transfer_block(struct CFGNode* node,
                           struct LiveSet live,
                           struct LiveSet aliased,
                           const struct VariableIndex* index) {
  reverse_block_body(&node->body);
  for (struct TACInstr* instr = node->body.head;
       instr != NULL;
       instr = instr->next) {
    transfer_instruction(instr, live, aliased, index);
  }
  reverse_block_body(&node->body);
}

// Allocate an empty queue capable of holding each CFG node exactly once.
static void block_queue_init(struct BlockQueue* queue, size_t capacity) {
  queue->items = dse_calloc(capacity, sizeof(*queue->items),
                            "creating the liveness work queue");
  queue->queued = dse_calloc(capacity, sizeof(*queue->queued),
                             "tracking queued CFG blocks");
  queue->capacity = capacity;
  queue->head = 0;
  queue->count = 0;
}

// Release queue storage and reset its fields.
static void block_queue_destroy(struct BlockQueue* queue) {
  free(queue->items);
  free(queue->queued);
  queue->items = NULL;
  queue->queued = NULL;
  queue->capacity = 0;
  queue->head = 0;
  queue->count = 0;
}

// Enqueue block_index unless it is already waiting. Exceeding capacity signals
// a broken queue invariant because duplicate entries are suppressed.
static void block_queue_push(struct BlockQueue* queue, unsigned block_index) {
  if (queue->queued[block_index]) {
    return;
  }
  if (queue->count >= queue->capacity) {
    fprintf(stderr,
            "Dead-store elimination error: liveness work queue exceeded %zu "
            "CFG nodes\n",
            queue->capacity);
    exit(1);
  }
  size_t tail = (queue->head + queue->count) % queue->capacity;
  queue->items[tail] = block_index;
  queue->queued[block_index] = true;
  queue->count += 1;
}

// Remove and return the oldest queued block index.
static unsigned block_queue_pop(struct BlockQueue* queue) {
  if (queue->count == 0) {
    fprintf(stderr,
            "Dead-store elimination error: attempted to pop an empty liveness "
            "work queue\n");
    exit(1);
  }
  unsigned block_index = queue->items[queue->head];
  queue->head = (queue->head + 1) % queue->capacity;
  queue->count -= 1;
  queue->queued[block_index] = false;
  return block_index;
}

// Solve backward live-variable dataflow to a fixed point. block_live_words
// receives one live-in bitset per CFG node; only changed blocks requeue their
// predecessors.
static void find_live_variables(struct CFG* cfg,
                                uint64_t* block_live_words,
                                struct LiveSet static_vars,
                                struct LiveSet aliased_vars,
                                const struct VariableIndex* index) {
  struct BlockQueue queue;
  block_queue_init(&queue, cfg->num_nodes);
  for (unsigned i = 1; i + 1 < cfg->num_nodes; ++i) {
    block_queue_push(&queue, i);
  }

  struct LiveSet live_out =
      live_set_allocate(static_vars.word_count, "computing block live-out sets");
  struct LiveSet live_in =
      live_set_allocate(static_vars.word_count, "computing block live-in sets");

  while (queue.count != 0) {
    unsigned block_index = block_queue_pop(&queue);
    struct CFGNode* block = cfg->nodes[block_index];
    meet_successors(cfg, block, block_live_words, static_vars, live_out);
    live_set_copy(live_in, live_out);
    transfer_block(block, live_in, aliased_vars, index);

    struct LiveSet old_live_in =
        block_live_set(block_live_words, live_in.word_count, block_index);
    if (!live_set_equal(old_live_in, live_in)) {
      live_set_copy(old_live_in, live_in);
      for (struct CFGNodeEntry* predecessor = block->predecessors.head;
           predecessor != NULL;
           predecessor = predecessor->next) {
        if (predecessor->node->type == CFG_ENTRY) {
          continue;
        }
        if (predecessor->node->type == CFG_EXIT) {
          fprintf(stderr,
                  "Dead-store elimination error: CFG exit node appears as a "
                  "predecessor\n");
          exit(1);
        }
        block_queue_push(&queue, cfg_node_index(cfg, predecessor->node));
      }
    }
  }

  free(live_out.words);
  free(live_in.words);
  block_queue_destroy(&queue);
}

// Mark dead instructions using the converged liveness solution, then unlink
// them in forward order. Transfer still visits instructions marked dead to
// preserve the previous pass's one-iteration behavior; `optimize()` reruns
// the pass until the TAC body reaches a fixed point.
static void eliminate_dead_instructions(struct CFG* cfg,
                                        uint64_t* block_live_words,
                                        struct LiveSet static_vars,
                                        struct LiveSet aliased_vars,
                                        const struct VariableIndex* index) {
  struct LiveSet live =
      live_set_allocate(static_vars.word_count, "eliminating dead instructions");

  for (unsigned block_index = 1;
       block_index + 1 < cfg->num_nodes;
       ++block_index) {
    struct CFGNode* block = cfg->nodes[block_index];
    meet_successors(cfg, block, block_live_words, static_vars, live);

    size_t instruction_count = 0;
    for (struct TACInstr* instr = block->body.head;
         instr != NULL;
         instr = instr->next) {
      instruction_count += 1;
    }
    bool* dead = dse_calloc(instruction_count, sizeof(*dead),
                            "marking dead instructions");

    reverse_block_body(&block->body);
    size_t reverse_index = instruction_count;
    for (struct TACInstr* instr = block->body.head;
         instr != NULL;
         instr = instr->next) {
      reverse_index -= 1;
      dead[reverse_index] = instruction_is_dead(instr, live, index);
      transfer_instruction(instr, live, aliased_vars, index);
    }
    reverse_block_body(&block->body);

    struct TACInstr* previous = NULL;
    struct TACInstr* instr = block->body.head;
    size_t instruction_index = 0;
    while (instr != NULL) {
      struct TACInstr* next = instr->next;
      if (dead[instruction_index]) {
        if (previous == NULL) {
          block->body.head = next;
        } else {
          previous->next = next;
        }
        if (instr == block->body.last) {
          block->body.last = previous;
        }
      } else {
        previous = instr;
      }
      instr = next;
      instruction_index += 1;
    }
    free(dead);
  }

  free(live.words);
}

// Perform backward live-variable analysis and remove assignments whose result
// is not live. All analysis state is local to this invocation; CFG/TAC nodes
// remain arena-owned.
struct CFG* dead_store_elim(struct CFG* cfg,
                            struct SliceList static_vars,
                            struct SliceList aliased_vars) {
  if (cfg == NULL || cfg->num_nodes <= 2) {
    return cfg;
  }

  struct VariableIndex index;
  variable_index_init(&index);
  collect_cfg_variables(&index, cfg, static_vars, aliased_vars);

  size_t word_count = (index.count + kLiveWordBits - 1) / kLiveWordBits;
  if (word_count == 0) {
    word_count = 1;
  }

  struct LiveSet static_set =
      live_set_allocate(word_count, "creating the static-variable liveness set");
  struct LiveSet aliased_set =
      live_set_allocate(word_count, "creating the aliased-variable liveness set");
  live_set_add_slice_list(static_set, &index, static_vars);
  live_set_add_slice_list(aliased_set, &index, aliased_vars);

  uint64_t* block_live_words =
      dse_calloc((size_t)cfg->num_nodes * word_count,
                 sizeof(*block_live_words),
                 "storing CFG live-in sets");

  find_live_variables(cfg, block_live_words, static_set, aliased_set, &index);
  eliminate_dead_instructions(cfg, block_live_words, static_set, aliased_set,
                              &index);

  free(block_live_words);
  free(static_set.words);
  free(aliased_set.words);
  variable_index_destroy(&index);
  return cfg;
}
