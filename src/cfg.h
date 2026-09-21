#ifndef CFG_H
#define CFG_H

#include "TAC.h"
#include "stdbool.h"

// Distinguish synthetic entry/exit nodes from executable basic blocks.
enum CFGNodeType {
  CFG_ENTRY,
  CFG_EXIT,
  CFG_BASIC_BLOCK,
};

// Link one predecessor or successor into a CFG adjacency list.
struct CFGNodeEntry {
  struct CFGNode* node;
  struct CFGNodeEntry* next;
};

// Own an insertion-ordered list of CFG edges.
struct CFGNodeList {
  struct CFGNodeEntry* head;
  struct CFGNodeEntry* tail;
};

// Represent one control-flow node with bidirectional edges and an optional TAC block.
struct CFGNode {
  enum CFGNodeType type;

  // Predecessor and successor links are bidirectional.

  struct CFGNodeList predecessors;
  struct CFGNodeList successors;

  struct TACInstrList body; // basic-block instructions; empty for entry/exit

  bool marked; // used for marking nodes during traversals
};

// Own a function's control-flow nodes; entry is first and exit is last.
// All intermediate nodes are basic blocks whose edges live on the nodes.
struct CFG {
  struct CFGNode** nodes;
  unsigned num_nodes;
};

// Partition a TAC function body into basic blocks and connect its control-flow edges.
struct CFG* build_cfg(struct TACInstr* body);

// Print an ASCII CFG visualization, including TAC and labeled outgoing edges.
void print_cfg(const struct CFG* cfg);

// Rebuild a linear TAC body from the CFG's current basic blocks.
struct TACInstr* rebuild_body(struct CFG* cfg);

// Clear traversal marks on every CFG node.
void reset_marks(struct CFG* cfg);

// After removing blocks from the CFG, nodes may contain dangling edges. 
// Repair the CFG to maintain consistency. Removes empty blocks as a side effect.
struct CFG* repair_cfg(struct CFG* cfg);

#endif // CFG_H
