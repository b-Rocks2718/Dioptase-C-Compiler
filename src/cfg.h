#ifndef CFG_H
#define CFG_H

#include "TAC.h"
#include "slice.h"
#include "stdbool.h"

struct AsmInstr;

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

// Represent one control-flow node with bidirectional edges and an optional
// instruction block. A TAC CFG (build_cfg, tac_cfg.h) stores copied instructions in body;
// an ASM CFG (build_asm_cfg) stores the inclusive range asm_head..asm_last of
// the function's own instruction list. The unused representation stays empty.
struct CFGNode {
  enum CFGNodeType type;
  unsigned index; // position in cfg->nodes; see cfg_number_nodes

  // Predecessor and successor links are bidirectional.

  struct CFGNodeList predecessors;
  struct CFGNodeList successors;

  struct TACInstrList body; // basic-block instructions; empty for entry/exit
  struct AsmInstr* asm_head; // first instruction of an ASM block, or NULL
  struct AsmInstr* asm_last; // last instruction of an ASM block, or NULL

  bool marked; // used for marking nodes during traversals
};

// Own a function's control-flow nodes; entry is first and exit is last.
// All intermediate nodes are basic blocks whose edges live on the nodes.
struct CFG {
  struct CFGNode** nodes;
  unsigned num_nodes;
};

// Control-flow role of one instruction: everything CFG construction and
// printing need to know about it.
enum CFGInstrKind {
  CFG_INSTR_OTHER,     // falls through to the next instruction
  CFG_INSTR_LABEL,     // starts a block; jumps name it
  CFG_INSTR_JUMP,      // ends a block; goes only to its target label
  CFG_INSTR_COND_JUMP, // ends a block; goes to its target label or falls through
  CFG_INSTR_RETURN,    // ends a block; goes to EXIT
  CFG_INSTR_TAIL_CALL, // ends a block; goes to EXIT
};

// Adapter between the IR-independent CFG code and one instruction IR.
// Instructions are passed as const void* pointing at that IR's instruction type.
struct CFGInstrOps {
  // Next instruction in a function body, or NULL at the end.
  const void* (*next)(const void* instr);
  // Control-flow role of instr. For labels and jumps, *label receives the
  // label's (or jump target's) name; it is left unchanged otherwise.
  enum CFGInstrKind (*classify)(const void* instr, const struct Slice** label);
  // Add instr to the end of a basic block's body.
  void (*append)(struct CFGNode* block, const void* instr);
  // First and last instruction of a node's body, or NULL when it is empty.
  const void* (*block_first)(const struct CFGNode* node);
  const void* (*block_last)(const struct CFGNode* node);
  // Print a non-empty basic block's instructions, indented one level.
  void (*print_block)(const struct CFGNode* node);
};

// Partition body into basic blocks and connect their control-flow edges. A
// block starts at a label or after a block-ending instruction. Exits on a
// jump to a label no block starts with.
struct CFG* build_cfg_with(const void* body, const struct CFGInstrOps* ops);

// Print a CFG built with ops: each block's instructions, its labeled outgoing
// edges, and an ASCII visualization.
void print_cfg_with(const struct CFG* cfg, const struct CFGInstrOps* ops);

// Append node at the end of list. An empty list has both head and tail NULL.
void cfg_node_list_append(struct CFGNodeList* list, struct CFGNode* node);

// Remove and return the oldest node, or NULL when list is empty.
struct CFGNode* cfg_node_list_remove_front(struct CFGNodeList* list);

// Return whether list contains no nodes.
bool cfg_node_list_is_empty(const struct CFGNodeList* list);

// Return whether list holds node. Membership is pointer identity.
bool cfg_node_list_contains(const struct CFGNodeList* list, const struct CFGNode* node);

// Set every node's index field to its position in cfg->nodes. build_cfg
// numbers new graphs; passes that index pass-local arrays by node call this on
// entry so the numbering is valid even for a CFG edited since construction.
void cfg_number_nodes(struct CFG* cfg);

// Clear traversal marks on every CFG node.
void reset_marks(struct CFG* cfg);

#endif // CFG_H
