#ifndef CFG_H
#define CFG_H

#include "TAC.h"

enum CFGNodeType {
  CFG_ENTRY,
  CFG_EXIT,
  CFG_BASIC_BLOCK,
};

// structs for making linked lists of CFGNodes

struct CFGNodeEntry {
  struct CFGNode* node;
  struct CFGNodeEntry* next;
};

struct CFGNodeList {
  struct CFGNodeEntry* head;
  struct CFGNodeEntry* tail;
};

struct CFGNode {
  enum CFGNodeType type;

  // linked list of predecessor and successor CFGNodes
  // links are bidirectional: `A` a successor of `B` <-> `B` a predecessor of `A`

  struct CFGNodeList predecessors;
  struct CFGNodeList successors;

  struct TACInstr* body;       // first instruction
  struct TACInstr* last_instr; // final instruction and O(1) append position
};

// a control flow graph, consisting of an array of CFGNodes and the total number of nodes
// links between nodes are stores within the nodes
// ENTRY node is always index 0, EXIT node is always index (num_nodes - 1)
// all other nodes are basic blocks
struct CFG {
  struct CFGNode** nodes;
  unsigned num_nodes;
};

// build a CFG for the body of a TAC function
struct CFG* build_cfg(struct TACInstr* body);

// rebuild the body of a TAC function from its CFG
struct TACInstr* rebuild_body(struct CFG* cfg);

#endif // CFG_H
