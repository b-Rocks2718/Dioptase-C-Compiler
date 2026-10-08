#include "interference_graph.h"
 
#include "asm_cfg.h"
#include "arena.h"

#include <stdbool.h>

// Creates a new pseudo-register interference node.
struct InterferenceNode* create_pseudo_interference_node(struct Slice* pseudo) {
  struct InterferenceNode* node = arena_alloc(sizeof(struct InterferenceNode));
  node->node_type = INTERFERENCE_PSEUDO;
  node->id.pseudo = pseudo;
  node->neighbors.head = NULL;
  node->neighbors.tail = NULL;
  node->num_neighbors = 0;
  node->spill_cost = 0;
  node->color = -1;
  node->pruned = false;
  return node;
}

// Creates a new physical register interference node.
struct InterferenceNode* create_reg_interference_node(enum Reg reg) {
  struct InterferenceNode* node = arena_alloc(sizeof(struct InterferenceNode));
  node->node_type = INTERFERENCE_REG;
  node->id.reg = reg;
  node->neighbors.head = NULL;
  node->neighbors.tail = NULL;
  node->num_neighbors = 0;
  node->spill_cost = 0;
  node->color = -1;
  node->pruned = false;
  return node;
}

// Checks if the given interference node list contains the specified node.
bool interference_node_list_contains(struct InterferenceNodeList* list, struct InterferenceNode* node) {
  struct InterferenceNodeEntry* current = list->head;
  while (current != NULL) {
    if (current->node == node) {
      return true;
    }
    current = current->next;
  }
  return false;
}

// appends a node to the given interference node list.
void interference_node_list_append(struct InterferenceNodeList* list, struct InterferenceNode* node) {
  struct InterferenceNodeEntry* entry = arena_alloc(sizeof(struct InterferenceNodeEntry));
  entry->node = node;
  entry->next = NULL;

  if (list->head == NULL) {
    list->head = entry;
    list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }
}

// Adds a node to the given interference node list if it is not already present.
void interference_node_list_add(struct InterferenceNodeList* list, struct InterferenceNode* node) {
  if (!interference_node_list_contains(list, node)) {
    interference_node_list_append(list, node);
  }
}

// Links two interference nodes, making them neighbors in the graph.
void link_interference_nodes(struct InterferenceNode* a, struct InterferenceNode* b) {
  interference_node_list_add(&a->neighbors, b);
  interference_node_list_add(&b->neighbors, a);
}

// Build base interference graph: all registers as nodes,
// every register inrterferes with every other register.
struct InterferenceGraph* build_base_graph() {
  struct InterferenceGraph* ig = arena_alloc(sizeof(struct InterferenceGraph));

  // add all physical register nodes to the graph

  // num of registers we use for allocation is 24:
  // R1 to R27, excluding R9 to R11
  ig->nodes.head = NULL;
  ig->nodes.tail = NULL;

  for (enum Reg r = R1; r <= R27; r++) {
    if (R9 <= r && r <= R11) { // TODO: make this a general policy, don't inline it here
      continue; // skip scratch registers R9 to R11
    }

    // create a node for each physical register
    interference_node_list_append(&ig->nodes, create_reg_interference_node(r));
  }

  // link each register to every other register
  for (struct InterferenceNodeEntry* current_i = ig->nodes.head; current_i != NULL; current_i = current_i->next) {
    for (struct InterferenceNodeEntry* current_j = current_i->next; current_j != NULL; current_j = current_j->next) {
      link_interference_nodes(current_i->node, current_j->node);
    }
  }
  return ig;
}

// Adds pseudo-register nodes to the interference graph based on the assembly instructions.
void add_pseudos(struct InterferenceGraph* ig, struct AsmInstr* instrs) {
  // make hashset to track pseudos that have already been added
  // can probably reuse existing hashmaps

  for (struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) { 
    // TODO: get pseudos from instr
    if (true /* TODO use hashmap */) {
      struct InterferenceNode* pseudo_node = create_pseudo_interference_node(NULL /* pseudo identifier */);
      interference_node_list_add(&ig->nodes, pseudo_node);
    }
  }
}

// Adds interference edges between nodes in the interference graph based on the control flow graph.
void add_interferences(struct InterferenceGraph* ig, struct AsmCFG* cfg) {
  // conservative analysis:
  //    if two vars are ever live at the same time, 
  //    they interfere with each other
  // more sophisticated analysis could use live ranges instead

  // TODO: implement this
}

// Builds the interference graph for the given assembly instructions.
struct InterferenceGraph* build_interference_graph(struct AsmInstr* instrs){
  struct InterferenceGraph* ig = build_base_graph();
  add_pseudos(ig, instrs);
  struct AsmCFG* cfg = build_asm_cfg(instrs);
  analyze_liveness(cfg);
  add_interferences(ig, cfg);
  return ig;
}

// Adds spill costs to the interference graph based on the given assembly instructions.
void add_spill_costs(struct InterferenceGraph* ig, struct AsmInstr* instrs){
  // TODO: implement this
}

// Colors the interference graph, assigning registers to pseudos where possible.
void color_graph(struct InterferenceGraph* ig){
  // TODO: implement graph coloring algorithm for register allocation
}
