#include "interference_graph.h"
 
#include "asm_cfg.h"
#include "checked_alloc.h"
#include "exit_codes.h"
#include "slice_index.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// left out keep their memory home.
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
      slice_index_add(&ig->pseudo_ids, opr->op.pseudo.name, &added);
      if (added) {
        union InterferenceNodeIdVariant id = { .pseudo = opr->op.pseudo.name };
        append_node(ig, INTERFERENCE_PSEUDO, id);
      }
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

// Adds interference edges between nodes in the interference graph based on the control flow graph.
void add_interferences(struct InterferenceGraph* ig, struct AsmCFG* cfg) {
  // conservative analysis:
  //    if two vars are ever live at the same time, 
  //    they interfere with each other
  // more sophisticated analysis could use live ranges instead

  // TODO: implement this
  puts("todo: add interferences\n");
  exit(1);
}

// Builds the interference graph for the given assembly instructions.
struct InterferenceGraph* build_interference_graph(struct AsmInstr* instrs){
  struct InterferenceGraph* ig = build_base_graph();
  add_pseudos(ig, instrs);
  allocate_edges(ig);
  struct AsmCFG* cfg = build_asm_cfg(instrs);
  analyze_liveness(cfg);
  add_interferences(ig, cfg);
  return ig;
}

// Adds spill costs to the interference graph based on the given assembly instructions.
void add_spill_costs(struct InterferenceGraph* ig, struct AsmInstr* instrs){
  // TODO: implement this
  puts("todo: add spill costs\n");
  exit(1);
}

// Colors the interference graph, assigning registers to pseudos where possible.
void color_graph(struct InterferenceGraph* ig){
  // TODO: implement graph coloring algorithm for register allocation
  puts("todo: color graph\n");
  exit(1);
}
