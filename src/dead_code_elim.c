#include "dead_code_elim.h"
#include "arena.h"
#include "TAC.h"
#include "cfg.h"

#include <stdio.h>
#include <stdlib.h>

// must set all marks to false before calling
void dfs_cfg(struct CFGNode* entry){
  if (entry == NULL || entry->marked) {
    return;
  }
  entry->marked = true;
  for (struct CFGNodeEntry* succ = entry->successors.head; succ != NULL; succ = succ->next) {
    dfs_cfg(succ->node);
  }
}

// Remove dead blocks, returning the new CFG
struct CFG* remove_dead_blocks(struct CFG* cfg){
  reset_marks(cfg);

  // dfs from entry node and mark every node we encounter
  dfs_cfg(cfg->nodes[0]); // first node is the entry node

  // always mark the exit node, even if it is unreachable
  // this way the exit node is not removed as dead code
  cfg->nodes[cfg->num_nodes - 1]->marked = true;

  // count marked nodes
  unsigned num_marked = 0;
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    if (cfg->nodes[i]->marked) {
      num_marked++;
    }
  }

  // create new cfg with only the marked nodes
  struct CFG* new_cfg = arena_alloc(sizeof(struct CFG));
  new_cfg->num_nodes = num_marked;
  new_cfg->nodes = arena_alloc(sizeof(struct CFGNode*) * num_marked);
  for (unsigned i = 0, j = 0; i < cfg->num_nodes; i++) {
    if (cfg->nodes[i]->marked) {
      new_cfg->nodes[j++] = cfg->nodes[i];
    }
  }

  return repair_cfg(new_cfg);
}

// Unlink the last instruction of a basic block, keeping last_instr and the TAC tail pointer consistent.
static void cfg_block_remove_last_instr(struct CFGNode* block) {
  struct TACInstr* last = block->last_instr;
  if (block->body == NULL || last == NULL) {
    return;
  }

  if (block->body == last) {
    block->body = NULL;
    block->last_instr = NULL;
  } else {
    struct TACInstr* prev = block->body;
    while (prev->next != NULL && prev->next != last) {
      prev = prev->next;
    }
    if (prev->next != last) {
      fprintf(stderr,
              "CFG error: cannot remove a block's last instruction because it "
              "is not reachable from the block body\n");
      exit(1);
    }
    prev->next = NULL;
    block->last_instr = prev;
    block->body->last = prev;
  }

  last->next = NULL;
  last->last = last;
}

// Drop a leading label from a basic block and retarget the remaining list's tail pointer.
static void cfg_block_remove_leading_label(struct CFGNode* block) {
  struct TACInstr* label = block->body;
  if (label == NULL || label->type != TACLABEL) {
    return;
  }

  struct TACInstr* rest = label->next;
  label->next = NULL;
  label->last = label;

  block->body = rest;
  if (rest == NULL) {
    block->last_instr = NULL;
  } else {
    rest->last = block->last_instr;
  }
}

// Remove useless jumps, modifying the CFG in place
void remove_useless_jumps(struct CFG* cfg){
  // loop over the basic block nodes
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* block = cfg->nodes[i];
    struct CFGNodeEntry* succ = block->successors.head;
    struct TACInstr* last = block->last_instr;

    if (succ == NULL || last == NULL) {
      continue;
    }

    // A jump is redundant when its only target is the next block.
    if (succ->node == cfg->nodes[i + 1] &&
        succ == block->successors.tail &&
        (last->type == TACJUMP || last->type == TACCOND_JUMP)) {
      cfg_block_remove_last_instr(block);
    }
  }
}

// Remove useless labels, modifying the CFG in place
void remove_useless_labels(struct CFG* cfg){
  // loop over the basic block nodes
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* block = cfg->nodes[i];
    // A label is redundant when the previous block is its only predecessor.
    if (block->predecessors.head == NULL ||
        (block->predecessors.head->node == cfg->nodes[i - 1] &&
        block->predecessors.head == block->predecessors.tail)) {
      cfg_block_remove_leading_label(block);
    }
  }
}

// Remove unreachable TAC blocks after control-flow analysis.
struct CFG* dead_code_elim(struct CFG* cfg){
  cfg = remove_dead_blocks(cfg);
  remove_useless_jumps(cfg);
  remove_useless_labels(cfg);
  cfg = repair_cfg(cfg);

  return cfg;
}
