#include "interference_graph.h"
 
#include "asm_cfg.h"
#include "checked_alloc.h"
#include "exit_codes.h"
#include "slice_index.h"
#include "codegen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// Dioptase general-purpose registers are 32 bits wide (docs/ISA.md), so only
// pseudos of at most this many bytes can live in one.
#define REGISTER_BYTES 4

// The registers the allocator may assign, in node-id order (see
// NUM_ALLOCATABLE_REGS for why the others are excluded).
static const enum Reg kAllocatableRegs[NUM_ALLOCATABLE_REGS] = {
  R1, R2, R3, R4, R5, R6, R7, R8,
  R11, R12, R13, R14, R15, R16, R17, R18, R19,
  R20, R21, R22, R23, R24, R25, R26, R27,
};

// Append a node with no edges, growing the node array as needed. Returns its id.
static size_t append_node(struct InterferenceGraph* ig, enum InterferenceNodeType type,
                          union InterferenceNodeIdVariant id) {
  if (ig->num_nodes == ig->nodes_capacity) {
    size_t capacity = ig->nodes_capacity * 2;
    struct InterferenceNode* nodes = checked_calloc(capacity, sizeof(*nodes), "Register allocation",
                                                    "growing the interference node array");
    memcpy(nodes, ig->nodes, ig->num_nodes * sizeof(*nodes));
    free(ig->nodes);
    ig->nodes = nodes;
    ig->nodes_capacity = capacity;
  }
  struct InterferenceNode* node = &ig->nodes[ig->num_nodes];
  node->node_type = type;
  node->id = id;
  node->neighbors.words = NULL;
  node->neighbors.word_count = 0;
  node->num_neighbors = 0;
  node->spill_cost = 0;
  node->color = -1;
  node->pruned = false;
  return ig->num_nodes++;
}

// Build base interference graph: one node per allocatable register, with ids
// 0..NUM_ALLOCATABLE_REGS-1. Register-register edges are added by
// allocate_edges once the node count is final.
static struct InterferenceGraph* build_base_graph(void) {
  struct InterferenceGraph* ig = checked_calloc(1, sizeof(*ig), "Register allocation",
                                                "creating the interference graph");
  ig->nodes_capacity = NUM_ALLOCATABLE_REGS * 2;
  ig->nodes = checked_calloc(ig->nodes_capacity, sizeof(*ig->nodes), "Register allocation",
                             "creating the interference graph");
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    union InterferenceNodeIdVariant id = { .reg = kAllocatableRegs[i] };
    append_node(ig, INTERFERENCE_REG, id);
  }
  return ig;
}

void interference_graph_add_edge(struct InterferenceGraph* ig, size_t a, size_t b) {
  if (a == b || bitset_test(ig->nodes[a].neighbors, b)) {
    return;
  }
  bitset_set(ig->nodes[a].neighbors, b);
  bitset_set(ig->nodes[b].neighbors, a);
  ig->nodes[a].num_neighbors++;
  ig->nodes[b].num_neighbors++;
}

// Allocate the adjacency matrix now that every node exists, point each node's
// neighbors at its row, and make every register interfere with every other
// register (distinct registers can never share a color).
static void allocate_edges(struct InterferenceGraph* ig) {
  size_t word_count = bitset_word_count(ig->num_nodes);
  if (ig->num_nodes > SIZE_MAX / word_count) {
    fprintf(stderr, "Register allocation error: adjacency matrix size overflow for %zu nodes\n",
            ig->num_nodes);
    exit(BCC_EXIT_INTERNAL);
  }
  ig->adjacency = checked_calloc(ig->num_nodes * word_count, sizeof(uint64_t),
                                 "Register allocation", "allocating the interference matrix");
  for (size_t i = 0; i < ig->num_nodes; i++) {
    ig->nodes[i].neighbors = bitset_view(ig->adjacency, word_count, i);
  }
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    for (size_t j = i + 1; j < NUM_ALLOCATABLE_REGS; j++) {
      interference_graph_add_edge(ig, i, j);
    }
  }
}

// Report a malformed pseudo found while building the interference graph.
static void interference_pseudo_error(const char* problem, const struct Slice* name) {
  fprintf(stderr, "Register allocation error: %s for pseudo '%.*s' while adding interference nodes\n",
          problem, name == NULL ? 6 : (int)name->len, name == NULL ? "<null>" : name->start);
  exit(BCC_EXIT_INTERNAL);
}

// Add the name of a pseudo operand to pinned if it is one.
static void pin_pseudo(struct SliceIndex* pinned, const struct Operand* opr) {
  if (opr == NULL) {
    return;
  }
  struct Slice* name = NULL;
  if (opr->type == OPERAND_PSEUDO) {
    name = opr->op.pseudo.name;
  } else if (opr->type == OPERAND_PSEUDO_MEM) {
    name = opr->op.pseudo_mem.name;
  } else {
    return;
  }
  if (name == NULL) {
    interference_pseudo_error("missing name", NULL);
  }
  slice_index_add(pinned, name, NULL);
}

// Collect the pseudos that must stay in memory however they are used
// elsewhere in the body. This needs its own pass because the deciding use can
// come after the pseudo's first appearance.
// - Address taken (GetAddress source): the pointer must refer to memory.
// - Accessed as PseudoMem: aggregates are addressed by byte offset.
// - The object of a volatile read or write: volatile objects keep their
//   memory home so every access really reaches memory
static void collect_memory_pseudos(struct AsmInstr* instrs, struct SliceIndex* pinned) {
  for (struct AsmInstr* instr = instrs; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      const struct Operand* opr = *slots.slot[i].field;
      if (opr == NULL) {
        continue;
      }
      if (opr->type == OPERAND_PSEUDO_MEM || slots.slot[i].role == OPERAND_ADDRESS) {
        pin_pseudo(pinned, opr);
      }
    }
    if (instr->type == ASM_VOLATILE_READ) {
      pin_pseudo(pinned, instr->instr.asm_volatile_read.src);
    } else if (instr->type == ASM_VOLATILE_WRITE) {
      pin_pseudo(pinned, instr->instr.asm_volatile_write.dst);
    }
  }
}

// Return true if opr is a pseudo that may be assigned a register: a scalar
// that fits one register, has automatic storage (statics become data labels),
// and is not pinned to memory.
static bool pseudo_is_allocatable(const struct Operand* opr, const struct SliceIndex* pinned) {
  if (opr == NULL || opr->type != OPERAND_PSEUDO) {
    return false;
  }
  struct Slice* name = opr->op.pseudo.name;
  if (name == NULL) {
    interference_pseudo_error("missing name", NULL);
  }
  if (slice_index_get(pinned, name) != SLICE_INDEX_NONE || is_static_symbol_operand(opr)) {
    return false;
  }
  struct AsmSymbolEntry* entry = asm_symbol_table_get(asm_symbol_table, name);
  if (entry == NULL || entry->type == NULL) {
    interference_pseudo_error("missing ASM symbol table entry", name);
  }
  return entry->type->type != BYTE_ARRAY && asm_type_size(entry->type) <= REGISTER_BYTES;
}

// Adds a node for every register-allocatable pseudo in instrs, in
// first-occurrence order, and records each one in ig->pseudo_ids. Pseudos
// left out keep their memory home. Each node's spill_cost is set to the
// number of operand slots naming its pseudo.
static void add_pseudos(struct InterferenceGraph* ig, struct AsmInstr* instrs) {
  slice_index_init(&ig->pseudo_ids, 128);

  struct SliceIndex pinned;
  slice_index_init(&pinned, 16);
  collect_memory_pseudos(instrs, &pinned);

  for (struct AsmInstr* instr = instrs; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      const struct Operand* opr = *slots.slot[i].field;
      if (!pseudo_is_allocatable(opr, &pinned)) {
        continue;
      }
      bool added = false;
      uint32_t id = slice_index_add(&ig->pseudo_ids, opr->op.pseudo.name, &added);
      if (added) {
        union InterferenceNodeIdVariant node_id = { .pseudo = opr->op.pseudo.name };
        append_node(ig, INTERFERENCE_PSEUDO, node_id);
      }
      ig->nodes[NUM_ALLOCATABLE_REGS + id].spill_cost++;
    }
  }

  slice_index_free(&pinned);
}

struct InterferenceNode* interference_graph_pseudo_node(struct InterferenceGraph* ig,
                                                        const struct Slice* name) {
  uint32_t id = slice_index_get(&ig->pseudo_ids, name);
  return id == SLICE_INDEX_NONE ? NULL : &ig->nodes[NUM_ALLOCATABLE_REGS + id];
}

struct InterferenceNode* interference_graph_reg_node(struct InterferenceGraph* ig, enum Reg reg) {
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    if (kAllocatableRegs[i] == reg) {
      return &ig->nodes[i];
    }
  }
  return NULL;
}

void destroy_interference_graph(struct InterferenceGraph* ig) {
  slice_index_free(&ig->pseudo_ids);
  free(ig->adjacency);
  free(ig->nodes);
  free(ig);
}

// ---------------------------------------------------------------------------
// Liveness
//
// Live sets are Bitsets over interference node ids, so only allocatable
// registers and register-candidate pseudos are tracked; memory-resident
// pseudos, scratch and fixed-role registers, and immediates never constrain
// allocation. The analysis is the standard backward one:
//   live_out(B) = union of live_in(S) over successors S
//   live_in(B)  = uses(B) | (live_out(B) - defs(B)), applied per instruction
// Blocks only link forward, so each block is walked backward through an array
// of its instruction pointers.
// ---------------------------------------------------------------------------

// No interference node: the operand or register is not a register candidate.
#define NO_NODE SIZE_MAX

// r1-r8 carry register arguments (docs/abi.md).
#define ABI_ARG_REGS 8

// Bound on the nodes one instruction reads: each operand slot can contribute
// its own value and a memory base register, and a call reads up to
// ABI_ARG_REGS argument registers.
#define MAX_INSTR_USES (2 * ASM_MAX_OPERAND_SLOTS + ABI_ARG_REGS)

// Bound on the nodes one instruction writes: a call clobbers every
// caller-saved allocatable register (a builtin fewer), and an instruction
// also writes its own destination.
#define MAX_INSTR_DEFS (NUM_ALLOCATABLE_REGS + 1)

// The interference nodes one instruction reads (uses) and writes (defs),
// including the ABI's implicit effects of calls and returns.
struct InstrEffect {
  size_t uses[MAX_INSTR_USES];
  size_t num_uses;
  size_t defs[MAX_INSTR_DEFS];
  size_t num_defs;
  // defs[0..num_explicit_defs) are the instruction's own written operand; the
  // rest are registers a call or builtin clobbers.
  size_t num_explicit_defs;
  // For a builtin Binary, its destination node: codegen writes it after the
  // builtin returns, so it never overlaps the clobbered registers. NO_NODE
  // otherwise.
  size_t builtin_dst;
  // For a Mov, its source node: a register-to-register copy does not make its
  // source and destination interfere, which lets them share a register.
  // NO_NODE for every other instruction.
  size_t move_src;
};

// Append node to an effect list unless it is NO_NODE.
static void effect_add(size_t* list, size_t* count, size_t capacity, size_t node) {
  if (node == NO_NODE) {
    return;
  }
  if (*count == capacity) {
    fprintf(stderr, "Register allocation error: instruction effect list overflow "
            "(capacity %zu) while computing liveness\n", capacity);
    exit(BCC_EXIT_INTERNAL);
  }
  list[(*count)++] = node;
}

// Node id for an allocatable register, or NO_NODE.
static size_t reg_node_id(struct InterferenceGraph* ig, enum Reg reg) {
  struct InterferenceNode* node = interference_graph_reg_node(ig, reg);
  return node == NULL ? NO_NODE : (size_t)(node - ig->nodes);
}

// Node id for the value an operand names: an allocatable register or a
// register-candidate pseudo. NO_NODE for everything else.
static size_t operand_node_id(struct InterferenceGraph* ig, const struct Operand* opr) {
  switch (opr->type) {
    case OPERAND_REG:
      return reg_node_id(ig, opr->op.reg.reg);
    case OPERAND_PSEUDO: {
      struct InterferenceNode* node = interference_graph_pseudo_node(ig, opr->op.pseudo.name);
      return node == NULL ? NO_NODE : (size_t)(node - ig->nodes);
    }
    default:
      return NO_NODE;
  }
}

// Read R1..R(count), checking count against the ABI's argument registers.
static void add_arg_reg_uses(struct InterferenceGraph* ig, struct InstrEffect* eff, size_t count,
                             const char* what) {
  if (count > ABI_ARG_REGS) {
    fprintf(stderr, "Register allocation error: %s reads %zu argument registers; "
            "the ABI has only r1-r%d\n", what, count, ABI_ARG_REGS);
    exit(BCC_EXIT_INTERNAL);
  }
  for (size_t i = 0; i < count; i++) {
    effect_add(eff->uses, &eff->num_uses, MAX_INSTR_USES, reg_node_id(ig, (enum Reg)(R1 + i)));
  }
}

// Last register an ordinary callee may overwrite: r1-r19 are caller-saved.
static const enum Reg kLastCallerSavedReg = R19;

// Last register an arithmetic helper (smul, sdiv, ...) may overwrite: the
// helpers may modify only r1-r8 ("Arithmetic Helper Routines", docs/abi.md).
static const enum Reg kLastHelperClobberedReg = R8;

// Write every allocatable register from r1 through last_clobbered: the callee
// may overwrite them, so nothing live across the call can occupy one.
static void add_clobber_defs(struct InstrEffect* eff, enum Reg last_clobbered) {
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    if (kAllocatableRegs[i] <= last_clobbered) {
      effect_add(eff->defs, &eff->num_defs, MAX_INSTR_DEFS, i);
    }
  }
}

// Compute the nodes instr reads and writes. Explicit operands come from
// asm_operand_slots; a Memory operand's base register is read whatever the
// slot's role. Implicit effects follow the ABI:
// - Call/IndirectCall: read the argument registers, clobber caller-saved ones.
// - TailCall/TailCallIndirect: read the argument registers; nothing in this
//   function runs afterwards, so their clobbers do not matter.
// - Binary ops lowered to builtins (multiply, divide, modulo): clobber only
//   r1-r8, the helpers' narrower contract (docs/abi.md); codegen passes the
//   operands in r1/r2 itself, after reading them.
// - Ret: read the registers holding the function's return value.
static void compute_instr_effect(struct InterferenceGraph* ig, const struct AsmFunc* func,
                                 struct AsmInstr* instr, struct InstrEffect* eff) {
  eff->num_uses = 0;
  eff->num_defs = 0;
  eff->builtin_dst = NO_NODE;
  eff->move_src = NO_NODE;

  struct OperandSlots slots = asm_operand_slots(instr);
  for (size_t i = 0; i < slots.count; i++) {
    const struct Operand* opr = *slots.slot[i].field;
    if (opr == NULL) {
      continue;
    }
    if (opr->type == OPERAND_MEMORY) {
      effect_add(eff->uses, &eff->num_uses, MAX_INSTR_USES, reg_node_id(ig, opr->op.memory.base));
    }
    switch (slots.slot[i].role) {
      case OPERAND_DEF:
        effect_add(eff->defs, &eff->num_defs, MAX_INSTR_DEFS, operand_node_id(ig, opr));
        break;
      case OPERAND_USE:
        effect_add(eff->uses, &eff->num_uses, MAX_INSTR_USES, operand_node_id(ig, opr));
        break;
      case OPERAND_ADDRESS:
        break; // only the address is formed; the value is not read
    }
  }

  eff->num_explicit_defs = eff->num_defs;

  switch (instr->type) {
    case ASM_MOV:
      eff->move_src = operand_node_id(ig, instr->instr.asm_mov.src);
      break;
    case ASM_CALL:
      add_arg_reg_uses(ig, eff, instr->instr.asm_call.num_reg_args, "Call");
      add_clobber_defs(eff, kLastCallerSavedReg);
      break;
    case ASM_INDIRECT_CALL:
      add_arg_reg_uses(ig, eff, instr->instr.asm_indirect_call.num_reg_args, "IndirectCall");
      add_clobber_defs(eff, kLastCallerSavedReg);
      break;
    case ASM_TAIL_CALL:
      add_arg_reg_uses(ig, eff, instr->instr.asm_tail_call.num_reg_args, "TailCall");
      break;
    case ASM_TAIL_CALL_INDIRECT:
      add_arg_reg_uses(ig, eff, instr->instr.asm_tail_call_indirect.num_reg_args,
                       "TailCallIndirect");
      break;
    case ASM_BINARY:
      if (alu_op_needs_builtin_call(instr->instr.asm_binary.alu_op)) {
        eff->builtin_dst = operand_node_id(ig, instr->instr.asm_binary.dst);
        add_clobber_defs(eff, kLastHelperClobberedReg);
      }
      break;
    case ASM_RET:
      add_arg_reg_uses(ig, eff, func->num_return_regs, "Ret");
      break;
    default:
      break;
  }
}

// Add an edge between node and every node set in live, except skip.
static void interfere_with_live(struct InterferenceGraph* ig, size_t node, struct Bitset live,
                                size_t skip) {
  for (size_t w = 0; w < live.word_count; w++) {
    uint64_t bits = live.words[w];
    for (size_t bit = 0; bits != 0; bit++, bits >>= 1) {
      size_t other = w * BITSET_WORD_BITS + bit;
      if ((bits & 1) != 0 && other != skip) {
        interference_graph_add_edge(ig, node, other);
      }
    }
  }
}

// Step live backward over one instruction: on entry it holds what is live
// after instr, on return what is live before it. With add_edges, first record
// that each node instr writes interferes with everything live after it, except
// a Mov's destination with its source and a builtin's clobbers with its result.
static void step_backward(struct InterferenceGraph* ig, const struct AsmFunc* func,
                          struct AsmInstr* instr, struct Bitset live, bool add_edges) {
  struct InstrEffect eff;
  compute_instr_effect(ig, func, instr, &eff);
  if (add_edges) {
    for (size_t i = 0; i < eff.num_defs; i++) {
      size_t skip = i < eff.num_explicit_defs ? eff.move_src : eff.builtin_dst;
      interfere_with_live(ig, eff.defs[i], live, skip);
    }
  }
  for (size_t i = 0; i < eff.num_defs; i++) {
    bitset_reset(live, eff.defs[i]);
  }
  for (size_t i = 0; i < eff.num_uses; i++) {
    bitset_set(live, eff.uses[i]);
  }
}

// Number of instructions in an ASM block.
static size_t block_length(const struct CFGNode* node) {
  size_t count = 0;
  for (const struct AsmInstr* instr = node->asm_head; instr != NULL; instr = instr->next) {
    count++;
    if (instr == node->asm_last) {
      break;
    }
  }
  return count;
}

// Walk a block backward, from live = its live-out set to live = its live-in
// set. buf must hold at least block_length(node) pointers.
static void walk_block_backward(struct InterferenceGraph* ig, const struct AsmFunc* func,
                                const struct CFGNode* node, struct AsmInstr** buf,
                                struct Bitset live, bool add_edges) {
  size_t count = 0;
  for (struct AsmInstr* instr = node->asm_head; instr != NULL; instr = instr->next) {
    buf[count++] = instr;
    if (instr == node->asm_last) {
      break;
    }
  }
  while (count > 0) {
    step_backward(ig, func, buf[--count], live, add_edges);
  }
}

// Solved liveness: block i's live-in set is
// bitset_view(live_in_words, word_count, i). EXIT's set stays empty; what a
// Ret or tail call reads is modeled on the instruction itself.
struct Liveness {
  uint64_t* live_in_words;
  size_t word_count;
  struct AsmInstr** block_buf; // scratch for walk_block_backward, sized to the largest block
};

// Set live to node's live-out set: the union of its successors' live-in sets.
static void block_live_out(const struct Liveness* lv, const struct CFGNode* node, struct Bitset live) {
  bitset_clear(live);
  for (const struct CFGNodeEntry* succ = node->successors.head; succ != NULL; succ = succ->next) {
    bitset_union(live, bitset_view(lv->live_in_words, lv->word_count, succ->node->index));
  }
}

// Solve liveness to a fixed point with a FIFO worklist, seeded with every
// block in reverse layout order since information flows backward. Sets only
// grow, so the solution is the same in any visit order.
static struct Liveness analyze_liveness(struct InterferenceGraph* ig, const struct AsmFunc* func,
                                        struct CFG* cfg) {
  struct Liveness lv;
  lv.word_count = bitset_word_count(ig->num_nodes);
  unsigned n = cfg->num_nodes;
  lv.live_in_words = checked_calloc((size_t)n * lv.word_count, sizeof(uint64_t),
                                    "Register allocation", "allocating block live-in sets");
  size_t max_block = 1;
  for (unsigned i = 0; i < n; i++) {
    size_t len = block_length(cfg->nodes[i]);
    if (len > max_block) {
      max_block = len;
    }
  }
  lv.block_buf = checked_calloc(max_block, sizeof(*lv.block_buf), "Register allocation",
                                "allocating the block instruction buffer");

  unsigned* queue = checked_calloc(n, sizeof(*queue), "Register allocation", "allocating the liveness worklist");
  bool* queued = checked_calloc(n, sizeof(*queued), "Register allocation", "allocating the liveness worklist");
  size_t head = 0;
  size_t count = 0;
  for (unsigned i = n - 1; i-- > 1; ) { // basic blocks only, last to first
    queue[(head + count++) % n] = i;
    queued[i] = true;
  }

  struct Bitset live = bitset_alloc(lv.word_count, "Register allocation", "computing block liveness");
  while (count != 0) {
    unsigned b = queue[head];
    head = (head + 1) % n;
    count--;
    queued[b] = false;

    struct CFGNode* block = cfg->nodes[b];
    block_live_out(&lv, block, live);
    walk_block_backward(ig, func, block, lv.block_buf, live, false);

    struct Bitset old_in = bitset_view(lv.live_in_words, lv.word_count, b);
    if (bitset_equal(old_in, live)) {
      continue;
    }
    bitset_copy(old_in, live);
    for (struct CFGNodeEntry* pred = block->predecessors.head; pred != NULL; pred = pred->next) {
      unsigned p = pred->node->index;
      if (pred->node->type == CFG_BASIC_BLOCK && !queued[p]) {
        queue[(head + count++) % n] = p;
        queued[p] = true;
      }
    }
  }

  bitset_free(&live);
  free(queue);
  free(queued);
  return lv;
}

// Add an edge wherever a written node meets a live one, walking every block
// backward from its solved live-out set.
static void add_interferences(struct InterferenceGraph* ig, const struct AsmFunc* func,
                              struct CFG* cfg, const struct Liveness* lv) {
  struct Bitset live = bitset_alloc(lv->word_count, "Register allocation", "adding interferences");
  for (unsigned i = 1; i + 1 < cfg->num_nodes; i++) {
    block_live_out(lv, cfg->nodes[i], live);
    walk_block_backward(ig, func, cfg->nodes[i], lv->block_buf, live, true);
  }
  bitset_free(&live);
}

// Builds the interference graph for func's body.
struct InterferenceGraph* build_interference_graph(struct AsmFunc* func) {
  struct InterferenceGraph* ig = build_base_graph();
  add_pseudos(ig, func->body);
  allocate_edges(ig);
  struct CFG* cfg = build_asm_cfg(func->body);
  struct Liveness lv = analyze_liveness(ig, func, cfg);
  add_interferences(ig, func, cfg, &lv);
  free(lv.live_in_words);
  free(lv.block_buf);
  return ig;
}

// Marks a node as pruned and updates the neighbor counts of its adjacent nodes.
static void prune_node(struct InterferenceGraph* ig, size_t i) {
  ig->nodes[i].pruned = true;

  // decrement the neighbor count for all neighbors of this node
  for (size_t j = 0; j < ig->num_nodes; j++) {
    if (i == j) continue;
    if (bitset_test(ig->nodes[i].neighbors, j)) {
      ig->nodes[j].num_neighbors--;
    }
  }
}

// Colors the interference graph, assigning registers to pseudos where possible.
void color_graph(struct InterferenceGraph* ig){
  // find the next node to prune

  size_t chosen_node = -1;
  size_t remaining_nodes = 0;

  for (size_t i = 0; i < ig->num_nodes; i++) {
    struct InterferenceNode node = ig->nodes[i];
    if (node.pruned) continue; // skip already pruned nodes
    remaining_nodes++;

    // check if the node can be pruned based on its degree
    if (node.num_neighbors < NUM_ALLOCATABLE_REGS) {
      prune_node(ig, i);
      chosen_node = i;
      break;
    }
  }

  if (remaining_nodes == 0) {
    // all nodes have been pruned, nothing left to color
    return;
  }

  if (chosen_node == -1) {
    // No node could be pruned, pick a node with the lowest metric to spill
    unsigned best_metric = UINT_MAX;

    for (size_t i = 0; i < ig->num_nodes; i++) {
      struct InterferenceNode node = ig->nodes[i];
      if (node.pruned) continue;
      if (node.node_type == INTERFERENCE_REG) continue; // skip hard registers
      // avoid division by zero by adding 1 to the denominator
      // avoid loss of precision by multiplying the numerator by 1000
      unsigned metric = (1000 * node.spill_cost) / (1 + node.num_neighbors);
      if (metric < best_metric) {
        best_metric = metric;
        chosen_node = i;
      }
    }
    if (chosen_node != -1) {
      prune_node(ig, chosen_node);
    }
  }

  if (chosen_node == -1) {
    // Unreachable while the graph is consistent: if every remaining node has
    // degree >= NUM_ALLOCATABLE_REGS, at least one of them is a pseudo (the
    // registers alone have degree NUM_ALLOCATABLE_REGS - 1), and pseudos are
    // always spill candidates.
    fprintf(stderr,
            "Register allocation error: no node can be pruned or spilled while coloring an "
            "interference graph of %zu nodes (%zu not yet pruned); every remaining node is a "
            "register with degree >= %d\n",
            ig->num_nodes, remaining_nodes, NUM_ALLOCATABLE_REGS);
    exit(BCC_EXIT_INTERNAL);
  }

  // we now have definitely pruned a node
  // recursively color the remaining graph
  color_graph(ig);

  // attempt to color the chosen node
  
  // start with all colors available
  unsigned available_colors_bitmask = (1 << NUM_ALLOCATABLE_REGS) - 1;

  for (size_t j = 0; j < ig->num_nodes; j++) {
    if (bitset_test(ig->nodes[chosen_node].neighbors, j)) {
      // if chosen node actually has this neighbor, check its color
      struct InterferenceNode neighbor = ig->nodes[j];
      if (neighbor.color != -1) {
        // remove colors used by this node's neighbors
        available_colors_bitmask &= ~(1 << neighbor.color);
      }
    }
  }
  if (available_colors_bitmask) {
    // if the the chosen node is a callee saved hard register,
    // assign it the maximum available color
    // else assign chosen node the lowest available color

    if (ig->nodes[chosen_node].node_type == INTERFERENCE_REG &&
        is_callee_saved(ig->nodes[chosen_node].id.reg)) {
      // assign the maximum available color
      for (int c = NUM_ALLOCATABLE_REGS - 1; c >= 0; c--) {
        if (available_colors_bitmask & (1 << c)) {
          ig->nodes[chosen_node].color = c;
          break;
        }
      }
    } else {
      // assign the lowest available color
      for (size_t c = 0; c < NUM_ALLOCATABLE_REGS; c++) {
        if (available_colors_bitmask & (1 << c)) {
          ig->nodes[chosen_node].color = c;
          break;
        }
      }
    }

    // mark the node as successfully colored
    ig->nodes[chosen_node].pruned = false;
  } else {
    // no available color, mark for spilling
    ig->nodes[chosen_node].color = -1;
  }
}
