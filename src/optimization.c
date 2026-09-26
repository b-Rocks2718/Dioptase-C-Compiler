#include "optimization.h"
#include "arena.h"
#include "TAC.h"
#include "cfg.h"
#include "call_graph.h"
#include "constant_fold.h"
#include "dead_code_elim.h"
#include "copy_prop.h"
#include "dead_store_elim.h"
#include "slice.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Return whether any implemented TAC/CFG optimization needs to run.
// Tail-call lowering happens while TAC is generated, and the remaining
// reserved options do not have passes yet, so they do not require a CFG.
static bool has_body_optimization(struct OptimizationOptions options) {
  return options.constant_fold || options.dead_code_elim || options.copy_prop ||
         options.dead_store_elim;
}

// Minimum slot count for NameSet; keep tables at most half full.
enum { kNameSetMinSlots = 16, kNameSetSlotsPerName = 2 };

// Fixed-capacity open-addressing set of names compared by content. Used to
// build duplicate-free name lists in linear time instead of rescanning the
// list for every insertion. Owns only its slot array; names stay arena-owned.
struct NameSet {
  struct Slice** slots;
  size_t slot_count; // power of two, >= kNameSetSlotsPerName * max_names
};

// Create a set able to hold max_names names.
static struct NameSet name_set_create(size_t max_names) {
  struct NameSet set;
  set.slot_count = kNameSetMinSlots;
  while (set.slot_count < max_names * kNameSetSlotsPerName) {
    set.slot_count *= 2;
  }
  set.slots = calloc(set.slot_count, sizeof(*set.slots));
  if (set.slots == NULL) {
    fprintf(stderr,
            "Optimizer error: unable to allocate a %zu-slot name set for "
            "collecting static and aliased variables\n",
            set.slot_count);
    exit(1);
  }
  return set;
}

// Insert name. Returns true if it was not already present.
static bool name_set_insert(struct NameSet* set, struct Slice* name) {
  size_t mask = set->slot_count - 1;
  size_t slot = hash_slice(name) & mask;
  while (set->slots[slot] != NULL) {
    if (compare_slice_to_slice(set->slots[slot], name)) {
      return false;
    }
    slot = (slot + 1) & mask;
  }
  set->slots[slot] = name;
  return true;
}

// Append name to list unless it is NULL or already in seen.
static void add_unique_name(struct SliceList* list, struct NameSet* seen, struct Slice* name) {
  if (name != NULL && name_set_insert(seen, name)) {
    slice_list_add(list, name);
  }
}

// Collect address-taken variables in body together with the already-computed
// program-wide static-variable set, each name once, statics first. Optimizer
// passes cannot introduce a new TACGET_ADDRESS, so this conservative set is
// invariant across fixed-point iterations for one function.
struct SliceList get_aliased_vars(struct TACInstr* body,
                                  struct SliceList static_vars) {
  size_t max_names = 0;
  for (struct SliceListNode* node = static_vars.head; node != NULL; node = node->next) {
    max_names++;
  }
  for (struct TACInstr* instr = body; instr != NULL; instr = instr->next) {
    if (instr->type == TACGET_ADDRESS) {
      max_names++;
    }
  }

  struct NameSet seen = name_set_create(max_names);
  struct SliceList list = {NULL, NULL};
  for (struct SliceListNode* node = static_vars.head; node != NULL; node = node->next) {
    add_unique_name(&list, &seen, node->slice);
  }
  for (struct TACInstr* instr = body; instr != NULL; instr = instr->next) {
    if (instr->type == TACGET_ADDRESS) {
      // src is a variable: typechecking rejects taking the address of a literal
      struct Val* src = instr->instr.tac_get_address.src;
      if (src != NULL && src->val_type == VARIABLE) {
        add_unique_name(&list, &seen, src->val.var_name);
      }
    }
  }
  free(seen.slots);
  return list;
}

// Return whether a symbol-table entry is a variable with static storage.
static bool is_static_entry(const struct SymbolEntry* entry) {
  return entry->attrs != NULL && entry->attrs->attr_type == STATIC_ATTR;
}

// Collect every static variable in the translation unit. Each name appears at
// most once, and the returned list nodes are allocated from the current arena.
struct SliceList get_static_vars(void) {
  struct SliceList list = {NULL, NULL};
  if (global_symbol_table == NULL) {
    return list;
  }

  size_t max_names = 0;
  for (size_t i = 0; i < global_symbol_table->size; i++) {
    for (struct SymbolEntry* entry = global_symbol_table->arr[i];
         entry != NULL;
         entry = entry->next) {
      if (is_static_entry(entry)) {
        max_names++;
      }
    }
  }

  struct NameSet seen = name_set_create(max_names);
  for (size_t i = 0; i < global_symbol_table->size; i++) {
    for (struct SymbolEntry* entry = global_symbol_table->arr[i];
         entry != NULL;
         entry = entry->next) {
      if (is_static_entry(entry)) {
        add_unique_name(&list, &seen, entry->key);
      }
    }
  }
  free(seen.slots);
  return list;
}

// Block size for optimizer scratch arenas. Iteration storage for one function
// (CFG nodes, edges, and instruction copies) is typically tens to hundreds of
// KiB, so larger blocks than the compilation arena's reduce malloc calls.
enum { kOptimizerScratchBlockSize = 64 * 1024 };

// Scratch storage for optimizing one function. See optimize_body for the
// lifetime of each arena.
struct OptimizerArenas {
  struct Arena* function;     // per-function data that must survive iterations
  struct Arena* iteration[2]; // alternating per-iteration storage
};

static struct TACInstrList optimize_body(
    struct TACInstrList body,
    struct OptimizationOptions options,
    struct SliceList static_vars,
    struct OptimizerArenas* arenas);

// Run the enabled body optimizations on every function until each TAC body
// reaches a fixed point. Translation-unit static names are collected once and
// reused, while address-taken names are invariant for each function.
void optimize(struct TACProg* prog, struct OptimizationOptions options) {
  if (prog == NULL || !has_body_optimization(options)) {
    return;
  }

  // Static storage duration is a translation-unit property. Reuse this set
  // across every function instead of rescanning the symbol table per function.
  struct SliceList static_vars = get_static_vars();

  struct OptimizerArenas arenas = {
      arena_create(kOptimizerScratchBlockSize),
      {arena_create(kOptimizerScratchBlockSize), arena_create(kOptimizerScratchBlockSize)},
  };
  for (struct TopLevel* top = prog->head; top != NULL; top = top->next) {
    if (top->type == FUNC) {
      top->top.tac_func.body =
          optimize_body(top->top.tac_func.body, options, static_vars, &arenas);
    }
  }
  arena_free(arenas.function);
  arena_free(arenas.iteration[0]);
  arena_free(arenas.iteration[1]);

  build_callgraph(prog);
}

// Copy body into the compilation arena. Operand pointers are shared with the
// source instructions; those operands are already compilation-lifetime data.
static struct TACInstrList copy_body_persistent(struct TACInstrList body) {
  struct TACInstrList copy = {NULL, NULL};
  for (struct TACInstr* instr = body.head; instr != NULL; instr = instr->next) {
    struct TACInstr* instr_copy =
        (struct TACInstr*)arena_alloc_persistent(sizeof(struct TACInstr));
    if (instr_copy == NULL) {
      fprintf(stderr,
              "Optimizer error: unable to allocate %zu bytes while copying an "
              "optimized TAC body into compilation storage\n",
              sizeof(struct TACInstr));
      exit(1);
    }
    *instr_copy = *instr;
    instr_copy->next = NULL;
    concat_TAC_instrs(&copy, tac_instr_list(instr_copy));
  }
  return copy;
}

// Run the enabled optimization passes over one function body while reusing the
// translation-unit static-variable set supplied by the caller.
//
// Memory lifetime: every CFG, instruction copy, and rebuilt body produced by
// an iteration is allocated in a scratch arena instead of the compilation
// arena. Iteration i allocates in arenas->iteration[i % 2] and reads the body
// rebuilt by iteration i - 1 from the other arena, so resetting
// iteration[i % 2] at the start of iteration i frees only iteration i - 2's
// storage, which nothing references any more. The fixed-point body is copied
// into the compilation arena before returning and all scratch arenas are reset.
// Data referenced by that final body but created during optimization (folded
// constant operands) is allocated with arena_alloc_persistent.
//
// Iteration 0 folds constants in place in the caller's body, which may link
// scratch instructions into it. The caller replaces its body with the return
// value and must not traverse the old list afterward.
static struct TACInstrList optimize_body(
    struct TACInstrList body,
    struct OptimizationOptions options,
    struct SliceList static_vars,
    struct OptimizerArenas* arenas) {
  if (body.head == NULL || !has_body_optimization(options)) {
    return body;
  }

  struct Arena* previous_arena = arena_set_current(arenas->function);
  struct SliceList aliased_vars =
      get_aliased_vars(body.head, static_vars);

  struct TACInstrList result;
  for (unsigned iteration = 0; ; iteration++) {
    struct Arena* iteration_arena = arenas->iteration[iteration % 2];
    arena_reset(iteration_arena);
    arena_set_current(iteration_arena);

    struct TACInstrList post_const_fold_body = body;
    if (options.constant_fold) {
      post_const_fold_body = constant_fold(body);
    }

    struct CFG* cfg = build_cfg(post_const_fold_body.head);

    if (options.dead_code_elim) {
      cfg = dead_code_elim(cfg);
    }

    if (options.copy_prop) {
      cfg = copy_prop(cfg, aliased_vars);
    }

    if (options.dead_store_elim) {
      cfg = dead_store_elim(cfg, static_vars, aliased_vars);
    }

    struct TACInstrList new_body = rebuild_body(cfg);
    if (compare_bodies(new_body.head, body.head)) {
      result = copy_body_persistent(new_body);
      break;
    }
    body = new_body;
  }

  arena_set_current(previous_arena);
  arena_reset(arenas->function);
  arena_reset(arenas->iteration[0]);
  arena_reset(arenas->iteration[1]);
  return result;
}
