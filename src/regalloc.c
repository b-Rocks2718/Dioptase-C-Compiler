#include "regalloc.h"

#include "arena.h"
#include "bitset.h"
#include "checked_alloc.h"
#include "codegen.h"
#include "exit_codes.h"
#include "interference_graph.h"
#include "union_find.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Report an inconsistent coloring of func's interference graph.
static void register_map_error(const struct AsmFunc* func, const char* fmt, ...) {
  fprintf(stderr, "Register allocation error in function %.*s: ", (int)func->name->len, func->name->start);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(BCC_EXIT_INTERNAL);
}

// color_graph's contract: every register node gets a distinct color in
// [0, NUM_ALLOCATABLE_REGS) (they all interfere with each other), and each
// pseudo gets one of those colors or -1 when spilled. A pseudo's register is
// the register node that shares its color. The coloring is checked rather than
// trusted, since a mistake here would silently put two live values in one
// register.
struct RegisterMap* build_register_map(struct InterferenceGraph* ig, struct AsmFunc* func) {
  // make a map from color to the corresponding register
  enum Reg color_regs[NUM_ALLOCATABLE_REGS];
  for (size_t c = 0; c < NUM_ALLOCATABLE_REGS; c++) {
    color_regs[c] = R0;
  }
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    const struct InterferenceNode* node = &ig->nodes[i];
    int color = node->color;
    if (color < 0 || color >= NUM_ALLOCATABLE_REGS) {
      register_map_error(func, "register node r%d has color %d; every register node needs a color "
                         "in 0..%d", (int)node->id.reg, color, NUM_ALLOCATABLE_REGS - 1);
    }
    if (color_regs[color] != R0) {
      register_map_error(func, "registers r%d and r%d share a color", (int)color_regs[color],
                         (int)node->id.reg);
    }
    color_regs[color] = node->id.reg;
  }

  struct RegisterMap* map = checked_calloc(1, sizeof(*map), "Register allocation", "creating the register map");
  map->pseudo_ids = &ig->pseudo_ids;
  map->num_pseudos = ig->num_nodes - NUM_ALLOCATABLE_REGS;
  map->regs = checked_calloc(map->num_pseudos, sizeof(*map->regs), "Register allocation",
                             "creating the register map");

  // assign psuedos to registers based on their color
  // also track the callee-saved registers used
  func->callee_saved_regs = 0;
  for (size_t id = 0; id < map->num_pseudos; id++) {
    int color = ig->nodes[NUM_ALLOCATABLE_REGS + id].color;
    if (color == -1) {
      // spill this pseudo
      map->regs[id] = R0;
      continue;
    }
    if (color < 0 || color >= NUM_ALLOCATABLE_REGS) {
      const struct Slice* name = ig->nodes[NUM_ALLOCATABLE_REGS + id].id.pseudo;
      register_map_error(func, "pseudo %.*s has color %d; expected -1 (spilled) or 0..%d",
                         (int)name->len, name->start, color, NUM_ALLOCATABLE_REGS - 1);
    }
    // assign the pseudo to the register corresponding to its color
    enum Reg reg = color_regs[color];
    map->regs[id] = reg;
    if (is_callee_saved(reg)) {
      func->callee_saved_regs |= UINT32_C(1) << reg;
    }
  }
  return map;
}

enum Reg register_map_get(const struct RegisterMap* map, const struct Slice* name) {
  uint32_t id = slice_index_get(map->pseudo_ids, name);
  return id == SLICE_INDEX_NONE ? R0 : map->regs[id];
}

void destroy_register_map(struct RegisterMap* map) {
  free(map->regs);
  free(map);
}

// Return a fresh Reg operand for reg with the given type. Operands may be
// shared between instructions, so replacements are new operands assigned to
// operand fields, never edits of the pseudo operand itself.
static struct Operand* make_reg_operand(enum Reg reg, struct AsmType* asm_type) {
  struct Operand* opr = arena_alloc(sizeof(*opr));
  opr->type = OPERAND_REG;
  opr->asm_type = asm_type;
  opr->op.reg.reg = reg;
  return opr;
}

// Return true if instr is a Mov from a register to the same register.
static bool is_self_move(const struct AsmInstr* instr) {
  if (instr->type != ASM_MOV) {
    return false;
  }
  const struct Operand* dst = instr->instr.asm_mov.dst;
  const struct Operand* src = instr->instr.asm_mov.src;
  return dst->type == OPERAND_REG && src->type == OPERAND_REG && dst->op.reg.reg == src->op.reg.reg;
}

// Replace every pseudo operand that coloring placed in a register with that register
void assign_registers(struct AsmFunc* func, struct RegisterMap* reg_map) {
  struct AsmInstr* prev = NULL;
  struct AsmInstr* instr = func->body;
  while (instr != NULL) {
    struct AsmInstr* next = instr->next;

    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      struct Operand* opr = *slots.slot[i].field;
      if (opr == NULL || opr->type != OPERAND_PSEUDO) {
        continue;
      }
      enum Reg reg = register_map_get(reg_map, opr->op.pseudo.name);
      if (reg != R0) {
        *slots.slot[i].field = make_reg_operand(reg, opr->asm_type);
      }
    }

    if (is_self_move(instr)) {
      // remove `mov rX, rX` instructions
      if (prev == NULL) {
        func->body = next;
      } else {
        prev->next = next;
      }
    } else {
      prev = instr;
    }
    instr = next;
  }
}

// Reserve a frame slot for each register in func->callee_saved_regs, just
// below the bytes already reserved (the return-buffer pointer, if any). Must
// run before assign_stack_slots, which places pseudos below
// reserved_stack_bytes.
static void reserve_callee_save_slots(struct AsmFunc* func) {
  func->callee_save_base = func->reserved_stack_bytes;
  for (unsigned r = 0; r < NUM_REGS; r++) {
    if ((func->callee_saved_regs >> r) & 1u) {
      func->reserved_stack_bytes += CALLEE_SAVE_SLOT_BYTES;
    }
  }
}

// Return the interference node that opr names, or NULL if opr is not a
// register or pseudo that has a node in ig.
static struct InterferenceNode* operand_node(struct InterferenceGraph* ig, const struct Operand* opr) {
  if (opr->type == OPERAND_REG) {
    return interference_graph_reg_node(ig, opr->op.reg.reg);
  }
  if (opr->type == OPERAND_PSEUDO) {
    return interference_graph_pseudo_node(ig, opr->op.pseudo.name);
  }
  return NULL;
}

// Returns true if node1 and node2 are neighbors in the interference graph.
static bool are_neighbors(struct InterferenceGraph* ig, struct InterferenceNode* node1, struct InterferenceNode* node2) {
  return bitset_test(node1->neighbors, node2 - ig->nodes);
}

// Perform the Briggs test for coalescing: returns true if
// two pseudos can be coalesced and the merged node
// is guaranteed to be pruned
static bool briggs_test(struct InterferenceGraph* ig, struct InterferenceNode* node1, struct InterferenceNode* node2) {
  unsigned significant_neighbors = 0;

  // we want to count the number of significant neighbors of a
  // hypothetical merged node

  // if # significant neighbors < NUM_ALLOCATABLE_REGS,
  // then the nonsignificant neighbors will be pruned,
  // leading to the merged node being pruned

  // if # significant neighbors >= NUM_ALLOCATABLE_REGS,
  // we cannot guarantee that the merged node will be pruned

  // first count the significant neighbors of node1
  for (unsigned i = 0; i < ig->num_nodes; i++) {
    if (bitset_test(node1->neighbors, i)) {
      struct InterferenceNode* neighbor = &ig->nodes[i];
      if (neighbor->num_neighbors >= NUM_ALLOCATABLE_REGS) {
        significant_neighbors++;
      }
    }
  }
  // then count the significant neighbors of node2 that are not already counted
  for (unsigned i = 0; i < ig->num_nodes; i++) {
    if (bitset_test(node2->neighbors, i)) {
      struct InterferenceNode* neighbor = &ig->nodes[i];
      if (neighbor->num_neighbors >= NUM_ALLOCATABLE_REGS &&
          !are_neighbors(ig, node1, neighbor)) {
        significant_neighbors++;
      }
    }
  }

  return significant_neighbors < NUM_ALLOCATABLE_REGS;
}

// Perform the George test for coalescing: 
// returns true if a pseudo can be coalesced with a register 
// without making graph coloring significantly harder
static bool george_test(struct InterferenceGraph* ig, struct InterferenceNode* reg_node, struct InterferenceNode* pseudo_node) {
  // more permissive than briggs test:
  //   coalesce if every neighbor satisfies one of two conditions:
  //     1. it has fewer than NUM_ALLOCATABLE_REGS neighbors, or
  //     2. it is already connected to the register node
  //   if the neighbor has fewer than NUM_ALLOCATABLE_REGS neighbors, 
  //     the neighbor will eventually be pruned, meaning we haven't made the 
  //     merged node harder to prune
  //   if the neighbor is already connected to the register node,
  //     it does not increase the difficulty of coloring the merged node

  for (unsigned i = 0; i < ig->num_nodes; i++) {
    if (bitset_test(pseudo_node->neighbors, i)) {
      struct InterferenceNode* neighbor = &ig->nodes[i];
      if (neighbor->num_neighbors < NUM_ALLOCATABLE_REGS) {
        continue;
      }
      if (are_neighbors(ig, neighbor, reg_node)) {
        continue;
      }
      return false;
    }
  }
  return true;
}

// Determines whether two nodes in the interference graph can be coalesced
// without making graph coloring significantly harder
static bool can_coalesce(struct InterferenceGraph* ig, struct InterferenceNode* src, struct InterferenceNode* dst) {
  if (briggs_test(ig, src, dst)) return true;
  if (src->node_type == INTERFERENCE_REG) return george_test(ig, src, dst);
  if (dst->node_type == INTERFERENCE_REG) return george_test(ig, dst, src);
  return false;
}

// Update the interference graph after coalescing two nodes:
// move all edges from to_merge to to_keep and isolate to_merge.
static void update_interference_graph_after_coalesce(struct InterferenceGraph* ig, unsigned to_keep, unsigned to_merge) {
  // to_merge's row is not modified by adding edges to to_keep, so it can be
  // scanned directly while edges are added.
  for (unsigned j = 0; j < ig->num_nodes; j++) {
    if (bitset_test(ig->nodes[to_merge].neighbors, j)) {
      interference_graph_add_edge(ig, to_keep, j);
    }
  }
  interference_graph_remove_node(ig, to_merge);
}

// Attempt to coalesce move-related nodes in the interference graph.
// Returns a union-find structure representing coalesced registers.
static struct UnionFind coalesce(struct InterferenceGraph* ig, struct AsmFunc* func){
  struct UnionFind coalesced_regs = uf_create(ig->num_nodes);
  
  for (struct AsmInstr* instr = func->body; instr != NULL; instr = instr->next) {
    if (instr->type != ASM_MOV) continue;
    struct Operand* src_op = instr->instr.asm_mov.src;
    struct Operand* dst_op = instr->instr.asm_mov.dst;

    // src and dst must each be a register or pseudo in the graph
    struct InterferenceNode* src_node = operand_node(ig, src_op);
    struct InterferenceNode* dst_node = operand_node(ig, dst_op);
    if (src_node == NULL || dst_node == NULL) continue;

    unsigned src_idx = uf_rep(&coalesced_regs, src_node - ig->nodes);
    unsigned dst_idx = uf_rep(&coalesced_regs, dst_node - ig->nodes);
    if (src_idx == dst_idx) continue;
    src_node = &ig->nodes[src_idx];
    dst_node = &ig->nodes[dst_idx];
    if (are_neighbors(ig, src_node, dst_node)) continue;
    if (!can_coalesce(ig, src_node, dst_node)) continue;

    // keep the lower id so hard registers win over pseudos
    unsigned to_keep = src_idx < dst_idx ? src_idx : dst_idx;
    unsigned to_merge = src_idx < dst_idx ? dst_idx : src_idx;

    uf_union(&coalesced_regs, to_keep, to_merge);
    update_interference_graph_after_coalesce(ig, to_keep, to_merge);
  }

  return coalesced_regs;
}

// Returns true if no unions were performed in the given union-find structure.
static bool none_coalesced(struct UnionFind uf) {
  return uf.num_unions == 0;
}

// Rewrite every register or pseudo operand in func to the node that survives
// coalescing for its set
static void rewrite_coalesced(struct AsmFunc* func, struct InterferenceGraph* ig,
                              struct UnionFind* coalesced_regs) {
  for (struct AsmInstr* instr = func->body; instr != NULL; instr = instr->next) {
    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      struct Operand* opr = *slots.slot[i].field;
      if (opr == NULL) {
        continue;
      }
      struct InterferenceNode* node = operand_node(ig, opr);
      if (node == NULL) {
        continue;
      }
      unsigned idx = (unsigned)(node - ig->nodes);
      unsigned keep = uf_rep(coalesced_regs, idx);
      if (keep == idx) {
        continue;
      }
      const struct InterferenceNode* keep_node = &ig->nodes[keep];
      if (keep_node->node_type == INTERFERENCE_REG) {
        *slots.slot[i].field = make_reg_operand(keep_node->id.reg, opr->asm_type);
      } else {
        struct Operand* repl = arena_alloc(sizeof(*repl));
        repl->type = OPERAND_PSEUDO;
        repl->asm_type = opr->asm_type;
        repl->op.pseudo.name = keep_node->id.pseudo;
        *slots.slot[i].field = repl;
      }
    }
  }
}

// Allocates registers for the body of a function based on the interference graph.
void alloc_body_registers(struct AsmFunc* func) {
  struct InterferenceGraph* ig = NULL;
  while (true) {
    ig = build_interference_graph(func);
    struct UnionFind coalesced_regs = coalesce(ig, func);
    if (none_coalesced(coalesced_regs)) {
      uf_destroy(&coalesced_regs);
      break;
    }
    rewrite_coalesced(func, ig, &coalesced_regs);
    destroy_interference_graph(ig);
    uf_destroy(&coalesced_regs);
  }
  color_graph(ig);
  struct RegisterMap* reg_map = build_register_map(ig, func);
  reserve_callee_save_slots(func);
  assign_registers(func, reg_map);
  destroy_register_map(reg_map);
  destroy_interference_graph(ig);
}

// Allocate registers for every function in prog. Pseudos left unallocated
// keep their memory home: assign_stack_slots gives them frame slots.
void allocate_registers(struct AsmProg* prog) {
  // iterate over functions and allocate registers for each one
  for (struct AsmTopLevel* top = prog->head; top != NULL; top = top->next) {
    // allocate registers for the function if this top-level item is a function
    if (top->type == ASM_FUNC) {
      alloc_body_registers(&top->top.asm_func);
    }
  }
}
