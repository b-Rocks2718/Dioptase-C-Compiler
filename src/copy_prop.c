#include "copy_prop.h"
#include "bitset.h"
#include "slice_index.h"
#include "checked_alloc.h"
#include "exit_codes.h"
#include "tac_cfg.h"
#include "arena.h"
#include "AST.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Copy propagation is a forward "reaching copies" dataflow analysis followed
// by an operand-rewriting walk. All analysis storage is pass-local heap memory
// released before copy_prop returns; nothing is retained in the compiler arena.
//
// Representation
// --------------
// A *copy class* is one distinct (dst, src) operand pair among the type-safe
// TACCOPY instructions of the function. Operands compare by value: variables
// by name and constants by raw bits (never by Val pointer or type), matching
// TAC operand equality. Classes are numbered in first-occurrence order (CFG
// blocks in layout order, instructions in list order), so a set of reaching
// copies is a fixed-width bitset over class numbers.
//
// Substitution source
// -------------------
// Two copies in one class can carry different src Val pointers whose types
// differ: copy_is_type_safe accepts constant 0 of any type, and classes
// compare constants by bits only. Rewriting substitutes:
// - the src of the instruction that generated the class earlier in the same
//   block, when the copy reached the use from inside the block, so the
//   substituted operand has the type of the copy that actually reached; otherwise
// - the src of the class's first occurrence.
// When several classes with the same dst reach a use (only possible for sets
// produced by the meet, never after a transfer kill/gen), every one of them
// holds there, so any choice is correct; the lowest-numbered class is simply
// what the bit scan finds first.
//
// Transfer-function note
// ----------------------
// "y = x" does not kill "x = y" when "x = y" already reaches (it is a no-op).
// The rule is sound, since the instruction cannot change any value, so every
// fixed point the solver reaches is correct. But it makes the transfer
// function non-monotone, so the usual guarantee that a worklist converges in
// any visit order does not apply. The worklist keeps the FIFO order the test
// suite exercises: every basic block in layout order first, then successors
// in edge order when a block's out-set changes. Changing the order needs a
// termination argument, or dropping the rule to make the transfer monotone.

// Hash table sizing used by the dense representation.
enum {
  kCopyPropMinHashSlots = 16,
  // Keep open-addressing tables at most half full: slots >= 2 * max entries.
  kCopyPropHashSlotsPerEntry = 2,
};
static const uint32_t kCopyPropNone = UINT32_MAX;
// A variable gets bitset masks when its class list has more than this many
// entries per bitset word (see CopyPropState::dst_mask).
enum { kMaskListRatio = 2 };

// Identify an operand by value: absent (NULL Val), constant bits, or variable id.
enum OperandKind {
  OPERAND_ABSENT,
  OPERAND_CONSTANT,
  OPERAND_VARIABLE,
};

// Value identity of a TAC operand. bits is constant bits or a variable id.
struct OperandKey {
  enum OperandKind kind;
  uint64_t bits;
};

// One distinct type-safe copy pair. dst/src are the first occurrence's operands.
struct CopyClass {
  struct Val* dst;
  struct Val* src;
  struct OperandKey dst_key;
  struct OperandKey src_key;
};

// What one instruction does to the reaching-copy set. Precomputed from the
// instruction's original operands before any rewriting takes place.
enum CopyEffectKind {
  COPY_EFFECT_NONE,
  COPY_EFFECT_COPY,         // TACCOPY: conditional kill of dst, then gen
  COPY_EFFECT_KILL,         // kill classes mentioning kill_var1 / kill_var2
  COPY_EFFECT_KILL_ALIASED, // kill aliased classes, then classes of kill_var1
};

// Precomputed transfer data for one instruction.
struct CopyEffect {
  enum CopyEffectKind kind;
  uint32_t kill_var1;   // variable id whose classes are killed, or kCopyPropNone
  uint32_t kill_var2;   // second killed variable id, or kCopyPropNone
  uint32_t own_class;   // TACCOPY: class equal to (dst, src), or kCopyPropNone
  uint32_t rev_class;   // TACCOPY: class equal to (src, dst), or kCopyPropNone
  bool generates;       // TACCOPY: copy is type-safe and becomes reaching
  struct Val* src;      // TACCOPY: source operand before rewriting
};

// Map variable names (by content) to dense ids. A NULL name is its own
// identity (null_id) because TAC operand equality treats two NULL names as
// equal; it is never killed, matching the previous implementation.
struct NameIndex {
  struct SliceIndex ids;
  uint32_t null_id; // id of the NULL name, or kCopyPropNone until it is numbered
};

// Per-invocation analysis state. Everything except cfg (borrowed) is owned
// and freed by copy_prop_state_destroy; the dst_mask/involve_mask entries are
// views into mask_storage rather than separate allocations.
struct CopyPropState {
  struct CFG* cfg;
  struct NameIndex names;

  struct CopyClass* classes;
  uint32_t num_classes;
  uint32_t* pair_slots;   // open-addressing (dst_key, src_key) -> class id
  size_t pair_slot_count; // power of two

  // CSR adjacency per variable id: classes whose dst is the variable (in
  // ascending class order), and classes that mention it as dst or src.
  uint32_t* dst_start;
  uint32_t* dst_list;
  uint32_t* involve_start;
  uint32_t* involve_list;

  // Bitset forms of the lists above, only for variables whose list is longer
  // than kMaskListRatio * words (views with words == NULL otherwise). A
  // variable written or read by many copies (a loop counter, an accumulator)
  // would otherwise make every kill or lookup walk hundreds of entries. The ratio bounds each mask's size
  // (words * 8 bytes) by its list's size (length * 4 bytes).
  struct Bitset* dst_mask;     // views into mask_storage
  struct Bitset* involve_mask; // views into mask_storage
  uint64_t* mask_storage;

  size_t words;               // words per class bitset
  struct Bitset aliased_mask; // classes whose dst or src is an aliased variable
  struct Bitset all_mask;     // every class

  struct CopyEffect* effects; // one per instruction in layout order
  size_t* node_first_effect;  // effects index of each CFG node's first instruction

  uint64_t* node_out;     // num_nodes * words: copies reaching each block's end (see node_out())
  struct Val** rep_src;   // current substitution source per class
};


// Return the smallest power of two >= max(value, kCopyPropMinHashSlots).
static size_t hash_slot_count_for(size_t entries) {
  size_t wanted = entries * kCopyPropHashSlotsPerEntry;
  size_t slots = kCopyPropMinHashSlots;
  while (slots < wanted) {
    slots *= 2;
  }
  return slots;
}

// Mix a 64-bit value so nearby ids and constants spread across hash slots.
static uint64_t mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

// ----- Name index -----

// Key standing in for a NULL name so absent operand names get an id from the
// same dense sequence; no identifier can equal it.
static struct Slice kNullNameKey = {"<null name>", 11};

// Initialize an empty name index sized for about max_names names.
static void name_index_init(struct NameIndex* index, size_t max_names) {
  slice_index_init(&index->ids, max_names);
  index->null_id = kCopyPropNone;
}

// Release the name index. Slice names remain arena-owned.
static void name_index_destroy(struct NameIndex* index) {
  slice_index_free(&index->ids);
}

// Return the id for name, inserting it when insert is true. Returns
// kCopyPropNone when name is absent and insert is false.
static uint32_t name_index_lookup(struct NameIndex* index, struct Slice* name, bool insert) {
  const struct Slice* key = name != NULL ? name : &kNullNameKey;
  if (insert) {
    uint32_t id = slice_index_add(&index->ids, key, NULL);
    if (name == NULL) {
      index->null_id = id;
    }
    return id;
  }
  uint32_t id = slice_index_get(&index->ids, key);
  return id == SLICE_INDEX_NONE ? kCopyPropNone : id;
}

// ----- Operand keys and class lookup -----

// Build the value key for val. Returns false when val is a variable whose name
// is not indexed and insert is false: such an operand cannot be in any class.
static bool operand_key(struct NameIndex* names, const struct Val* val, bool insert,
                        struct OperandKey* key) {
  if (val == NULL) {
    key->kind = OPERAND_ABSENT;
    key->bits = 0;
    return true;
  }
  if (val->val_type == CONSTANT) {
    key->kind = OPERAND_CONSTANT;
    key->bits = val->val.const_value;
    return true;
  }
  uint32_t id = name_index_lookup(names, val->val.var_name, insert);
  if (id == kCopyPropNone) {
    return false;
  }
  key->kind = OPERAND_VARIABLE;
  key->bits = id;
  return true;
}

// Return true when two operand keys denote the same TAC operand.
static bool operand_key_equal(struct OperandKey a, struct OperandKey b) {
  return a.kind == b.kind && a.bits == b.bits;
}

// Hash a (dst, src) key pair into the pair table.
static size_t pair_hash(struct OperandKey dst, struct OperandKey src) {
  uint64_t h = mix64(((uint64_t)dst.kind << 62) ^ dst.bits);
  h = mix64(h ^ ((uint64_t)src.kind << 60) ^ (src.bits * 0x9e3779b97f4a7c15ULL));
  return (size_t)h;
}

// Find the class for (dst, src). With insert, a missing pair becomes a new
// class whose first-occurrence operands are dst_val/src_val.
static uint32_t class_lookup(struct CopyPropState* s,
                             struct OperandKey dst, struct OperandKey src,
                             bool insert, struct Val* dst_val, struct Val* src_val) {
  size_t mask = s->pair_slot_count - 1;
  size_t slot = pair_hash(dst, src) & mask;
  while (s->pair_slots[slot] != kCopyPropNone) {
    const struct CopyClass* c = &s->classes[s->pair_slots[slot]];
    if (operand_key_equal(c->dst_key, dst) && operand_key_equal(c->src_key, src)) {
      return s->pair_slots[slot];
    }
    slot = (slot + 1) & mask;
  }
  if (!insert) {
    return kCopyPropNone;
  }
  uint32_t id = s->num_classes++;
  s->classes[id].dst = dst_val;
  s->classes[id].src = src_val;
  s->classes[id].dst_key = dst;
  s->classes[id].src_key = src;
  s->pair_slots[slot] = id;
  return id;
}

// Return the class equal to (dst, src) by value without inserting.
static uint32_t class_find_vals(struct CopyPropState* s,
                                const struct Val* dst, const struct Val* src) {
  struct OperandKey dst_key = {OPERAND_ABSENT, 0};
  struct OperandKey src_key = {OPERAND_ABSENT, 0};
  if (!operand_key(&s->names, dst, false, &dst_key) ||
      !operand_key(&s->names, src, false, &src_key)) {
    return kCopyPropNone;
  }
  return class_lookup(s, dst_key, src_key, false, NULL, NULL);
}

// ----- Helpers shared with the rest of the compiler -----

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

// ----- Setup -----

// Return the id of the variable named by val for kill purposes. Constants,
// NULL operands, NULL names, and names outside every class kill nothing.
static uint32_t kill_id_for_val(struct CopyPropState* s, const struct Val* val) {
  if (val == NULL || val->val_type != VARIABLE || val->val.var_name == NULL) {
    return kCopyPropNone;
  }
  return name_index_lookup(&s->names, val->val.var_name, false);
}

// Return the id of a variable given by name for kill purposes.
static uint32_t kill_id_for_name(struct CopyPropState* s, struct Slice* name) {
  if (name == NULL) {
    return kCopyPropNone;
  }
  return name_index_lookup(&s->names, name, false);
}

// Describe how instr changes the reaching-copy set. Mirrors the kill/gen
// rules: writes kill copies mentioning the written variable; calls, stores,
// and volatile loads additionally kill copies mentioning aliased variables.
static struct CopyEffect compute_effect(struct CopyPropState* s, struct TACInstr* instr) {
  struct CopyEffect e = {
      COPY_EFFECT_NONE, kCopyPropNone, kCopyPropNone,
      kCopyPropNone, kCopyPropNone, false, NULL,
  };
  switch (instr->type) {
    case TACCOPY: {
      struct Val* dst = instr->instr.tac_copy.dst;
      struct Val* src = instr->instr.tac_copy.src;
      e.kind = COPY_EFFECT_COPY;
      e.kill_var1 = kill_id_for_val(s, dst);
      e.own_class = class_find_vals(s, dst, src);
      e.rev_class = class_find_vals(s, src, dst);
      e.generates = copy_is_type_safe(src, dst);
      e.src = src;
      break;
    }
    case TACVOLATILE_READ:
      // The read observes a value that may change. Do not record dst = src,
      // or a later use of dst could be rewritten back into the volatile object.
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_copy.dst);
      e.kill_var2 = kill_id_for_val(s, instr->instr.tac_copy.src);
      break;
    case TACVOLATILE_WRITE:
      // The write must stay, and a later read of dst must not reuse src.
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_copy.dst);
      break;
    case TACCALL:
      e.kind = COPY_EFFECT_KILL_ALIASED;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_call.dst);
      break;
    case TACCALL_INDIRECT:
      e.kind = COPY_EFFECT_KILL_ALIASED;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_call_indirect.dst);
      break;
    case TACUNARY:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_unary.dst);
      break;
    case TACBINARY:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_binary.dst);
      break;
    case TACSTORE:
    case TACVOLATILE_STORE:
      e.kind = COPY_EFFECT_KILL_ALIASED;
      break;
    case TACTRUNC:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_trunc.dst);
      break;
    case TACEXTEND:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_extend.dst);
      break;
    case TACLOAD:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_load.dst);
      break;
    case TACVOLATILE_LOAD:
      // A volatile memory read can observe an external update of any aliased object.
      e.kind = COPY_EFFECT_KILL_ALIASED;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_load.dst);
      break;
    case TACGET_ADDRESS:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_get_address.dst);
      break;
    case TACCOPY_TO_OFFSET:
    case TACVOLATILE_COPY_TO_OFFSET:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_name(s, instr->instr.tac_copy_to_offset.dst);
      break;
    case TACCOPY_FROM_OFFSET:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_copy_from_offset.dst);
      break;
    case TACVOLATILE_COPY_FROM_OFFSET:
      e.kind = COPY_EFFECT_KILL;
      e.kill_var1 = kill_id_for_val(s, instr->instr.tac_copy_from_offset.dst);
      e.kill_var2 = kill_id_for_name(s, instr->instr.tac_copy_from_offset.src);
      break;
    case TACRETURN:
    case TACCOND_JUMP:
    case TACJUMP:
    case TACLABEL:
    case TACBOUNDARY:
    case TACTAIL_CALL:          // these go directly to EXIT, so they
    case TACTAIL_CALL_INDIRECT: // do not need to kill any copies
      break;
  }
  return e;
}

// Append var's CSR entry for class c. cursor holds each variable's next write slot.
static void csr_push(uint32_t* list, uint32_t* cursor, uint32_t var, uint32_t c) {
  list[cursor[var]++] = c;
}

// Build per-variable class lists from the finished class table. Each list is
// filled in ascending class order because classes are visited in order.
static void build_variable_lists(struct CopyPropState* s) {
  uint32_t vars = s->names.ids.count;
  s->dst_start = checked_calloc((size_t)vars + 1, sizeof(uint32_t), "Copy propagation", "indexing copy destinations");
  s->involve_start = checked_calloc((size_t)vars + 1, sizeof(uint32_t), "Copy propagation", "indexing copy operands");

  // Count, then prefix-sum into start offsets (standard CSR construction).
  for (uint32_t c = 0; c < s->num_classes; ++c) {
    const struct CopyClass* cls = &s->classes[c];
    uint32_t dst_var = (uint32_t)cls->dst_key.bits;
    s->dst_start[dst_var + 1]++;
    // Copies are killed by name; a NULL name never kills (see NameIndex).
    if (dst_var != s->names.null_id) {
      s->involve_start[dst_var + 1]++;
    }
    if (cls->src_key.kind == OPERAND_VARIABLE &&
        (uint32_t)cls->src_key.bits != s->names.null_id &&
        cls->src_key.bits != cls->dst_key.bits) {
      s->involve_start[cls->src_key.bits + 1]++;
    }
  }
  for (uint32_t v = 0; v < vars; ++v) {
    s->dst_start[v + 1] += s->dst_start[v];
    s->involve_start[v + 1] += s->involve_start[v];
  }

  s->dst_list = checked_calloc(s->dst_start[vars], sizeof(uint32_t), "Copy propagation", "indexing copy destinations");
  s->involve_list = checked_calloc(s->involve_start[vars], sizeof(uint32_t), "Copy propagation", "indexing copy operands");
  uint32_t* dst_cursor = checked_calloc(vars, sizeof(uint32_t), "Copy propagation", "indexing copy destinations");
  uint32_t* involve_cursor = checked_calloc(vars, sizeof(uint32_t), "Copy propagation", "indexing copy operands");
  for (uint32_t v = 0; v < vars; ++v) {
    dst_cursor[v] = s->dst_start[v];
    involve_cursor[v] = s->involve_start[v];
  }
  for (uint32_t c = 0; c < s->num_classes; ++c) {
    const struct CopyClass* cls = &s->classes[c];
    uint32_t dst_var = (uint32_t)cls->dst_key.bits;
    csr_push(s->dst_list, dst_cursor, dst_var, c);
    if (dst_var != s->names.null_id) {
      csr_push(s->involve_list, involve_cursor, dst_var, c);
    }
    if (cls->src_key.kind == OPERAND_VARIABLE &&
        (uint32_t)cls->src_key.bits != s->names.null_id &&
        cls->src_key.bits != cls->dst_key.bits) {
      csr_push(s->involve_list, involve_cursor, (uint32_t)cls->src_key.bits, c);
    }
  }
  free(dst_cursor);
  free(involve_cursor);
}

// Return true when a CSR list of len entries should also get a bitset mask.
static bool wants_mask(const struct CopyPropState* s, uint32_t len) {
  return (size_t)len > s->words * kMaskListRatio;
}

// Build bitset masks for variables with long destination/operand lists.
static void build_variable_masks(struct CopyPropState* s) {
  uint32_t vars = s->names.ids.count;
  s->dst_mask = checked_calloc(vars, sizeof(struct Bitset), "Copy propagation", "indexing heavily copied variables");
  s->involve_mask = checked_calloc(vars, sizeof(struct Bitset), "Copy propagation", "indexing heavily copied variables");
  size_t masks = 0;
  for (uint32_t v = 0; v < vars; ++v) {
    masks += wants_mask(s, s->dst_start[v + 1] - s->dst_start[v]);
    masks += wants_mask(s, s->involve_start[v + 1] - s->involve_start[v]);
  }
  // With no masks there is nothing to allocate; return before deriving
  // pointers from mask storage.
  if (masks == 0) {
    return;
  }
  if (s->words == 0) {
    fprintf(stderr,
            "Copy propagation error: cannot build %zu variable masks with "
            "zero words per mask\n",
            masks);
    exit(BCC_EXIT_INTERNAL);
  }
  if (masks > SIZE_MAX / s->words) {
    fprintf(stderr,
            "Copy propagation error: allocation size overflow while indexing "
            "heavily copied variables\n");
    exit(BCC_EXIT_INTERNAL);
  }
  size_t mask_words = masks * s->words;
  s->mask_storage = checked_calloc(mask_words, sizeof(uint64_t),
                              "Copy propagation", "indexing heavily copied variables");
  // The sizes above are nonzero and checked_calloc terminates on allocation
  // failure. Keep the sparse-list representation as a defensive fallback if
  // that helper's contract is not visible to a caller or static analyzer.
  if (s->mask_storage == NULL) {
    return;
  }
  size_t next = 0;
  for (uint32_t v = 0; v < vars; ++v) {
    if (wants_mask(s, s->dst_start[v + 1] - s->dst_start[v])) {
      s->dst_mask[v] = bitset_view(s->mask_storage, s->words, next++);
      for (uint32_t k = s->dst_start[v]; k < s->dst_start[v + 1]; ++k) {
        bitset_set(s->dst_mask[v], s->dst_list[k]);
      }
    }
    if (wants_mask(s, s->involve_start[v + 1] - s->involve_start[v])) {
      s->involve_mask[v] = bitset_view(s->mask_storage, s->words, next++);
      for (uint32_t k = s->involve_start[v]; k < s->involve_start[v + 1]; ++k) {
        bitset_set(s->involve_mask[v], s->involve_list[k]);
      }
    }
  }
}

// Mark classes that mention an aliased variable. aliased_vars is the set of
// statics plus address-taken locals; calls and stores may modify any of them.
static void build_aliased_mask(struct CopyPropState* s, struct SliceList aliased_vars) {
  uint32_t vars = s->names.ids.count;
  bool* aliased = checked_calloc(vars, sizeof(bool), "Copy propagation", "marking aliased variables");
  for (struct SliceListNode* node = aliased_vars.head; node != NULL; node = node->next) {
    if (node->slice == NULL) {
      continue;
    }
    uint32_t id = name_index_lookup(&s->names, node->slice, false);
    if (id != kCopyPropNone) {
      aliased[id] = true;
    }
  }
  s->aliased_mask = bitset_alloc(s->words, "Copy propagation", "marking aliased copies");
  for (uint32_t c = 0; c < s->num_classes; ++c) {
    const struct CopyClass* cls = &s->classes[c];
    uint32_t dst_var = (uint32_t)cls->dst_key.bits;
    bool mentions = dst_var != s->names.null_id && aliased[dst_var];
    if (cls->src_key.kind == OPERAND_VARIABLE &&
        (uint32_t)cls->src_key.bits != s->names.null_id &&
        aliased[cls->src_key.bits]) {
      mentions = true;
    }
    if (mentions) {
      bitset_set(s->aliased_mask, c);
    }
  }
  free(aliased);
}

// Collect classes, effects, and index structures for cfg. Returns false when
// the function has no type-safe copies, in which case nothing can change and
// the caller skips the analysis entirely.
static bool copy_prop_state_init(struct CopyPropState* s, struct CFG* cfg,
                                 struct SliceList aliased_vars) {
  memset(s, 0, sizeof(*s));
  s->cfg = cfg;

  // Number nodes so pass-local arrays can be indexed from CFG edge pointers.
  cfg_number_nodes(cfg);
  size_t num_instrs = 0;
  size_t num_copy_instrs = 0;
  for (unsigned i = 0; i < cfg->num_nodes; ++i) {
    for (struct TACInstr* instr = cfg->nodes[i]->body.head; instr != NULL; instr = instr->next) {
      num_instrs++;
      if (instr->type == TACCOPY) {
        num_copy_instrs++;
      }
    }
  }
  if (num_copy_instrs == 0) {
    return false;
  }

  // Pass 1: discover classes in first-occurrence order. Each copy adds at
  // most two names and one class, which bounds both fixed-size tables.
  name_index_init(&s->names, num_copy_instrs * 2);
  s->classes = checked_calloc(num_copy_instrs, sizeof(*s->classes), "Copy propagation", "collecting copies");
  s->pair_slot_count = hash_slot_count_for(num_copy_instrs);
  s->pair_slots = checked_calloc(s->pair_slot_count, sizeof(uint32_t), "Copy propagation", "collecting copies");
  memset(s->pair_slots, 0xff, s->pair_slot_count * sizeof(uint32_t)); // kCopyPropNone
  for (unsigned i = 1; i + 1 < cfg->num_nodes; ++i) {
    for (struct TACInstr* instr = cfg->nodes[i]->body.head; instr != NULL; instr = instr->next) {
      if (instr->type != TACCOPY) {
        continue;
      }
      struct Val* dst = instr->instr.tac_copy.dst;
      struct Val* src = instr->instr.tac_copy.src;
      if (!copy_is_type_safe(src, dst)) {
        continue;
      }
      struct OperandKey dst_key = {OPERAND_ABSENT, 0};
      struct OperandKey src_key = {OPERAND_ABSENT, 0};
      operand_key(&s->names, dst, true, &dst_key);
      operand_key(&s->names, src, true, &src_key);
      if (dst_key.kind != OPERAND_VARIABLE) {
        fprintf(stderr,
                "Copy propagation error: TACCOPY destination in CFG block %u is "
                "not a variable (operand kind %d); expected a writable variable\n",
                i, (int)dst->val_type);
        exit(BCC_EXIT_INTERNAL);
      }
      class_lookup(s, dst_key, src_key, true, dst, src);
    }
  }
  if (s->num_classes == 0) {
    return false;
  }

  s->words = bitset_word_count(s->num_classes);
  s->all_mask = bitset_alloc(s->words, "Copy propagation", "building the copy universe");
  for (uint32_t c = 0; c < s->num_classes; ++c) {
    bitset_set(s->all_mask, c);
  }
  build_variable_lists(s);
  build_variable_masks(s);
  build_aliased_mask(s, aliased_vars);

  // Pass 2: precompute every instruction's effect now that all names and
  // classes are known (a later copy can create the reverse of an earlier one).
  s->effects = checked_calloc(num_instrs, sizeof(*s->effects), "Copy propagation", "precomputing copy effects");
  s->node_first_effect = checked_calloc((size_t)cfg->num_nodes + 1, sizeof(size_t),
                                   "Copy propagation", "precomputing copy effects");
  size_t next_effect = 0;
  for (unsigned i = 0; i < cfg->num_nodes; ++i) {
    s->node_first_effect[i] = next_effect;
    for (struct TACInstr* instr = cfg->nodes[i]->body.head; instr != NULL; instr = instr->next) {
      s->effects[next_effect++] = compute_effect(s, instr);
    }
  }
  s->node_first_effect[cfg->num_nodes] = next_effect;

  s->node_out = checked_calloc((size_t)cfg->num_nodes * s->words, sizeof(uint64_t),
                          "Copy propagation", "allocating block reaching-copy sets");
  s->rep_src = checked_calloc(s->num_classes, sizeof(*s->rep_src), "Copy propagation", "tracking copy sources");
  for (uint32_t c = 0; c < s->num_classes; ++c) {
    s->rep_src[c] = s->classes[c].src;
  }
  return true;
}

// Release all pass-local storage owned by s.
static void copy_prop_state_destroy(struct CopyPropState* s) {
  name_index_destroy(&s->names);
  free(s->classes);
  free(s->pair_slots);
  free(s->dst_start);
  free(s->dst_list);
  free(s->involve_start);
  free(s->involve_list);
  free(s->dst_mask);
  free(s->involve_mask);
  free(s->mask_storage);
  bitset_free(&s->aliased_mask);
  bitset_free(&s->all_mask);
  free(s->effects);
  free(s->node_first_effect);
  free(s->node_out);
  free(s->rep_src);
  memset(s, 0, sizeof(*s));
}

// ----- Dataflow -----

// Return the reaching-copy set at the end of the node with index i.
static struct Bitset node_out(struct CopyPropState* s, unsigned i) {
  return bitset_view(s->node_out, s->words, i);
}

// Remove every class that mentions variable var (as dst or src).
static void kill_var(struct CopyPropState* s, struct Bitset state, uint32_t var) {
  if (var == kCopyPropNone) {
    return;
  }
  if (s->involve_mask[var].words != NULL) {
    bitset_subtract(state, s->involve_mask[var]);
    return;
  }
  for (uint32_t k = s->involve_start[var]; k < s->involve_start[var + 1]; ++k) {
    bitset_reset(state, s->involve_list[k]);
  }
}

// Return the index of the lowest set bit of a nonzero word. Written without
// compiler builtins so the compiler stays portable to the Dioptase toolchain.
static uint32_t lowest_set_bit(uint64_t word) {
  uint32_t bit = 0;
  while ((word & 0xffffffffu) == 0) { word >>= 32; bit += 32; }
  while ((word & 0xffu) == 0) { word >>= 8; bit += 8; }
  while ((word & 1u) == 0) { word >>= 1; bit += 1; }
  return bit;
}

// Track rep_src overrides made while walking one block so they can be undone.
struct RepUndo {
  uint32_t* classes;
  size_t count;
};

// Apply one instruction's effect to state. When undo is non-NULL, a generated
// class substitutes this instruction's own source until the block ends.
static void apply_effect(struct CopyPropState* s, const struct CopyEffect* e,
                         struct Bitset state, struct RepUndo* undo) {
  switch (e->kind) {
    case COPY_EFFECT_NONE:
      break;
    case COPY_EFFECT_COPY:
      // y = x is a no-op if x = y already reaches, so it must not kill x = y.
      if (e->rev_class != kCopyPropNone && bitset_test(state, e->rev_class)) {
        break;
      }
      kill_var(s, state, e->kill_var1);
      if (e->generates) {
        bitset_set(state, e->own_class);
        if (undo != NULL && s->rep_src[e->own_class] != e->src) {
          s->rep_src[e->own_class] = e->src;
          undo->classes[undo->count++] = e->own_class;
        }
      }
      break;
    case COPY_EFFECT_KILL:
      kill_var(s, state, e->kill_var1);
      kill_var(s, state, e->kill_var2);
      break;
    case COPY_EFFECT_KILL_ALIASED:
      bitset_subtract(state, s->aliased_mask);
      kill_var(s, state, e->kill_var1);
      break;
  }
}

// Compute the copies reaching the start of node: the intersection of its
// predecessors' out-sets. A block following ENTRY has no incoming copies; a
// block with no predecessors (unreachable) receives every copy.
static void meet(struct CopyPropState* s, const struct CFGNode* node, struct Bitset state) {
  bitset_copy(state, s->all_mask);
  for (struct CFGNodeEntry* pred = node->predecessors.head; pred != NULL; pred = pred->next) {
    if (pred->node->type == CFG_ENTRY) {
      bitset_clear(state);
      return;
    }
    if (pred->node->type == CFG_EXIT) {
      fprintf(stderr,
              "Copy propagation error: CFG block %u has the EXIT node as a "
              "predecessor; EXIT must not have successors\n",
              node->index);
      exit(BCC_EXIT_INTERNAL);
    }
    bitset_intersect(state, node_out(s, pred->node->index));
  }
}

// Apply every instruction's effect in node to state (in-set -> out-set).
static void transfer_block(struct CopyPropState* s, unsigned node_index, struct Bitset state) {
  for (size_t k = s->node_first_effect[node_index]; k < s->node_first_effect[node_index + 1]; ++k) {
    apply_effect(s, &s->effects[k], state, NULL);
  }
}

// Solve reaching copies to a fixed point. Every block starts with the full
// universe (optimistic initialization), then a FIFO worklist propagates changes.
static void find_reaching_copies(struct CopyPropState* s, struct Bitset state) {
  struct CFG* cfg = s->cfg;
  unsigned n = cfg->num_nodes;
  unsigned* queue = checked_calloc(n, sizeof(unsigned), "Copy propagation", "allocating the copy worklist");
  bool* queued = checked_calloc(n, sizeof(bool), "Copy propagation", "allocating the copy worklist");
  size_t head = 0;
  size_t count = 0;

  for (unsigned i = 1; i + 1 < n; ++i) {
    bitset_copy(node_out(s, i), s->all_mask);
    queue[(head + count++) % n] = i;
    queued[i] = true;
  }

  while (count != 0) {
    unsigned b = queue[head];
    head = (head + 1) % n;
    count--;
    queued[b] = false;

    struct CFGNode* block = cfg->nodes[b];
    meet(s, block, state);
    transfer_block(s, b, state);

    struct Bitset out = node_out(s, b);
    if (bitset_equal(out, state)) {
      continue;
    }
    bitset_copy(out, state);

    // The out-set changed, so every successor must be revisited.
    for (struct CFGNodeEntry* succ = block->successors.head; succ != NULL; succ = succ->next) {
      if (succ->node->type == CFG_EXIT) {
        continue;
      }
      if (succ->node->type == CFG_ENTRY) {
        fprintf(stderr,
                "Copy propagation error: CFG block %u has the ENTRY node as a "
                "successor; ENTRY must not have predecessors\n",
                b);
        exit(BCC_EXIT_INTERNAL);
      }
      unsigned succ_index = succ->node->index;
      if (!queued[succ_index]) {
        queue[(head + count++) % n] = succ_index;
        queued[succ_index] = true;
      }
    }
  }

  free(queue);
  free(queued);
}

// ----- Rewriting -----

// Replace operand with the source of a reaching copy whose dst is operand.
static struct Val* replace_operand(struct CopyPropState* s, struct Val* operand,
                                   struct Bitset state) {
  if (operand == NULL || operand->val_type == CONSTANT) {
    return operand;
  }
  uint32_t var = name_index_lookup(&s->names, operand->val.var_name, false);
  if (var == kCopyPropNone) {
    return operand;
  }
  struct Bitset mask = s->dst_mask[var];
  if (mask.words != NULL) {
    // Lowest-numbered reaching class with this dst, as in the list walk below.
    for (size_t w = 0; w < s->words; ++w) {
      uint64_t hits = state.words[w] & mask.words[w];
      if (hits != 0) {
        return s->rep_src[w * BITSET_WORD_BITS + lowest_set_bit(hits)];
      }
    }
    return operand;
  }
  for (uint32_t k = s->dst_start[var]; k < s->dst_start[var + 1]; ++k) {
    uint32_t c = s->dst_list[k];
    if (bitset_test(state, c)) {
      return s->rep_src[c];
    }
  }
  return operand;
}

// Return true when a and b are the same operand with the same type pointer:
// identical constant bits or variable names (by content). Used to decide
// whether rewriting a call argument actually changed it.
static bool same_operand_value(const struct Val* a, const struct Val* b) {
  if (a->val_type != b->val_type || a->type != b->type) {
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

// Rewrite call arguments copy-on-write and return the (possibly new) array.
//
// CFG and rebuilt-body instructions are shallow copies, so an argument array
// is shared with every earlier version of the call, including the previous
// iteration's body that optimize_body compares against. compare_instrs checks
// argument arrays by pointer, so rewriting the shared array in place would
// hide the change from the fixed-point test. Instead, a changed argument list
// gets a fresh array. It is allocated in the compilation arena because later
// instruction copies (and the final TAC body) share it beyond the lifetime of
// the optimizer's scratch arenas. An argument whose value is unchanged (for
// example, already rewritten by a previous iteration) does not count as a
// change, so repeated iterations reach a fixed point.
static struct Val* replace_args(struct CopyPropState* s, struct Val* args, size_t num_args,
                                struct Bitset state) {
  struct Val* rewritten = args;
  for (size_t i = 0; i < num_args; i++) {
    const struct Val* replacement = replace_operand(s, &args[i], state);
    if (same_operand_value(replacement, &args[i])) {
      continue;
    }
    if (rewritten == args) {
      rewritten = (struct Val*)arena_alloc_persistent(num_args * sizeof(struct Val));
      if (rewritten == NULL) {
        fprintf(stderr,
                "Copy propagation error: unable to allocate %zu bytes for a "
                "rewritten %zu-argument call\n",
                num_args * sizeof(struct Val), num_args);
        exit(BCC_EXIT_INTERNAL);
      }
      memcpy(rewritten, args, num_args * sizeof(struct Val));
    }
    rewritten[i] = *replacement;
  }
  return rewritten;
}

// Rewrite the instruction by replacing its operands with their reaching copies
// if they exist. effect was computed from this instruction's original operands.
// Returns true if the instruction can be deleted (is redundant), false otherwise.
static bool rewrite_instr(struct CopyPropState* s, struct TACInstr* instr,
                          const struct CopyEffect* effect, struct Bitset state) {
  // A copy is redundant when x = y or y = x already reaches.
  if (instr->type == TACCOPY &&
      ((effect->own_class != kCopyPropNone && bitset_test(state, effect->own_class)) ||
       (effect->rev_class != kCopyPropNone && bitset_test(state, effect->rev_class)))) {
    return true;
  }

  struct TACOperands ops = tac_instr_operands(instr);
  for (size_t i = 0; i < ops.count; i++) {
    struct TACOperand* op = &ops.op[i];
    // Only values that are read can be replaced. Directly named aggregates
    // (offset copies) and address-taken operands name storage, not values.
    if (op->role != TAC_USE || op->kind == TAC_OPERAND_NAME) {
      continue;
    }
    if (op->kind == TAC_OPERAND_ARGS) {
      *op->args = replace_args(s, *op->args, op->num_args, state);
      continue;
    }
    // A volatile read's source is the volatile object itself and must be read.
    if (instr->type == TACVOLATILE_READ) {
      continue;
    }
    // Store addresses are not rewritten; propagating into them would be a
    // further optimization.
    if ((instr->type == TACSTORE || instr->type == TACVOLATILE_STORE) &&
        op->val == &instr->instr.tac_store.dst_ptr) {
      continue;
    }
    *op->val = replace_operand(s, *op->val, state);
  }
  return false;
}

// Rewrite every block using the solved in-sets. Each instruction is rewritten
// against the copies reaching it, then its precomputed effect advances the set.
static void rewrite_blocks(struct CopyPropState* s, struct Bitset state) {
  struct CFG* cfg = s->cfg;
  struct RepUndo undo;
  undo.classes = NULL;
  undo.count = 0;
  size_t max_block_instrs = 0;
  for (unsigned i = 1; i + 1 < cfg->num_nodes; ++i) {
    size_t block_instrs = s->node_first_effect[i + 1] - s->node_first_effect[i];
    if (block_instrs > max_block_instrs) {
      max_block_instrs = block_instrs;
    }
  }
  // At most one override per generating instruction in a block.
  undo.classes = checked_calloc(max_block_instrs, sizeof(uint32_t), "Copy propagation", "tracking copy sources");

  for (unsigned i = 1; i + 1 < cfg->num_nodes; ++i) {
    struct CFGNode* block = cfg->nodes[i];
    meet(s, block, state);

    const struct CopyEffect* effect = &s->effects[s->node_first_effect[i]];
    struct TACInstr* prev_instr = NULL;
    for (struct TACInstr* instr = block->body.head; instr != NULL; instr = instr->next, ++effect) {
      if (rewrite_instr(s, instr, effect, state)) {
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
      apply_effect(s, effect, state, &undo);
    }

    // Restore first-occurrence sources before the next block.
    for (size_t k = 0; k < undo.count; ++k) {
      uint32_t c = undo.classes[k];
      s->rep_src[c] = s->classes[c].src;
    }
    undo.count = 0;
  }
  free(undo.classes);
}

// Perform copy propagation on the given CFG. The CFG's instructions are
// rewritten in place; redundant copies are unlinked from their blocks.
struct CFG* copy_prop(struct CFG* cfg, struct SliceList aliased_vars) {
  struct CopyPropState s;
  if (!copy_prop_state_init(&s, cfg, aliased_vars)) {
    // No type-safe copies: every reaching set is empty and nothing changes.
    copy_prop_state_destroy(&s);
    return cfg;
  }

  struct Bitset state = bitset_alloc(s.words, "Copy propagation", "allocating a reaching-copy set");
  find_reaching_copies(&s, state);
  rewrite_blocks(&s, state);
  bitset_free(&state);
  copy_prop_state_destroy(&s);
  return cfg;
}
