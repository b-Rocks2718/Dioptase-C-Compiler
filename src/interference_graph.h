#ifndef INTERFERENCE_GRAPH_H
#define INTERFERENCE_GRAPH_H

#include "asm_gen.h"
#include "asm_cfg.h"
#include <stdbool.h>

// Types of interference nodes: physical register or pseudo-register.
enum InterferenceNodeType {
  INTERFERENCE_REG,
  INTERFERENCE_PSEUDO
};

// Represents the identifier of an interference node, 
// which can be either a physical register or a pseudo-register.
union InterferenceNodeIdVariant {
  enum Reg reg;
  struct Slice* pseudo;
};

// Represents an entry in the linked list of interference nodes.
struct InterferenceNodeEntry {
  struct InterferenceNode* node;
  struct InterferenceNodeEntry* next;
};

// Represents a linked list of interference nodes.
struct InterferenceNodeList {
  struct InterferenceNodeEntry* head;
  struct InterferenceNodeEntry* tail;
};

// Represents a node in the interference graph, 
// corresponding to a register or pseudo-register.
struct InterferenceNode {
  enum InterferenceNodeType node_type;
  union InterferenceNodeIdVariant id;
  struct InterferenceNodeList neighbors;
  unsigned num_neighbors;
  int spill_cost;
  int color;
  bool pruned;
};

// Represents the interference graph used for register allocation
struct InterferenceGraph {
  struct InterferenceNodeList nodes;
};

// Builds the interference graph for the given assembly instructions.
struct InterferenceGraph* build_interference_graph(struct AsmInstr* instrs);

// Adds spill costs to the interference graph based on the given assembly instructions.
void add_spill_costs(struct InterferenceGraph* ig, struct AsmInstr* instrs);

// Colors the interference graph, assigning registers to pseudos where possible.
void color_graph(struct InterferenceGraph* ig);

#endif // INTERFERENCE_GRAPH_H