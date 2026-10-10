#include "tac_cfg.h"
#include "exit_codes.h"

#include <stdio.h>
#include <stdlib.h>

// TAC side of the CFG: the CFGInstrOps adapter that lets the IR-independent
// builder and printer in cfg.c handle TAC bodies, plus the TAC-only
// operations (rebuilding a body from blocks, repairing an edited CFG).

// Add a detached copy of a TAC instruction to a basic block node.
static void tac_append(struct CFGNode* block, const void* opaque_instr) {
  const struct TACInstr* instr = opaque_instr;
  if (block->type != CFG_BASIC_BLOCK) {
    fprintf(stderr,
            "CFG error: cannot append TAC instruction type %d to CFG node type %d; "
            "expected a basic block\n",
            instr->type, block->type);
    exit(BCC_EXIT_INTERNAL);
  }

  struct TACInstr* instr_copy = copy_instr(instr);
  concat_TAC_instrs(&block->body, tac_instr_list(instr_copy));
}

// Next TAC instruction in a function body.
static const void* tac_next(const void* instr) {
  return ((const struct TACInstr*)instr)->next;
}

// Control-flow role of a TAC instruction.
static enum CFGInstrKind tac_classify(const void* opaque_instr, const struct Slice** label) {
  const struct TACInstr* instr = opaque_instr;
  switch (instr->type) {
    case TACLABEL:
      *label = instr->instr.tac_label.label;
      return CFG_INSTR_LABEL;
    case TACJUMP:
      *label = instr->instr.tac_jump.label;
      return CFG_INSTR_JUMP;
    case TACCOND_JUMP:
      *label = instr->instr.tac_cond_jump.label;
      return CFG_INSTR_COND_JUMP;
    case TACRETURN:
      return CFG_INSTR_RETURN;
    case TACTAIL_CALL:
    case TACTAIL_CALL_INDIRECT:
      return CFG_INSTR_TAIL_CALL;
    default:
      return CFG_INSTR_OTHER;
  }
}

// First instruction of a TAC block, or NULL.
static const void* tac_block_first(const struct CFGNode* node) {
  return node->body.head;
}

// Last instruction of a TAC block, or NULL.
static const void* tac_block_last(const struct CFGNode* node) {
  return node->body.last;
}

// Print a TAC block's instructions.
static void tac_print_block(const struct CFGNode* node) {
  for (const struct TACInstr* instr = node->body.head; instr != NULL; instr = instr->next) {
    print_tac_instr(instr, 1);
  }
}

// How the shared CFG builder and printer in cfg.c see TAC instructions.
static const struct CFGInstrOps kTACCFGOps = {
  .next = tac_next,
  .classify = tac_classify,
  .append = tac_append,
  .block_first = tac_block_first,
  .block_last = tac_block_last,
  .print_block = tac_print_block,
};

// build a CFG for the body of a TAC function
struct CFG* build_cfg(struct TACInstr* body) {
  return build_cfg_with(body, &kTACCFGOps);
}

void print_cfg(const struct CFG* cfg) {
  print_cfg_with(cfg, &kTACCFGOps);
}

// Rebuild a detached TAC function body from basic blocks in layout order. The
// copies keep CFG block lists independent, so rebuilding does not consume or
// otherwise mutate the graph.
struct TACInstrList rebuild_body(struct CFG* cfg) {
  if (cfg == NULL || cfg->nodes == NULL) {
    fprintf(stderr,
            "CFG error: cannot rebuild a TAC body from a null or uninitialized CFG\n");
    exit(BCC_EXIT_INTERNAL);
  }

  struct TACInstrList rebuilt = tac_instr_list(NULL);

  // add instructions from each basic block in layout order
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    struct CFGNode* node = cfg->nodes[i];
    if (node == NULL || node->type != CFG_BASIC_BLOCK || node->body.head == NULL) {
      continue;
    }
    if (node->body.last == NULL) {
      fprintf(stderr,
              "CFG error: cannot rebuild basic block %u because its non-empty "
              "body has no last instruction\n",
              i);
      exit(BCC_EXIT_INTERNAL);
    }

    struct TACInstr* instr = node->body.head;
    // add each instruction from a basic block
    while (true) {
      struct TACInstr* instr_copy = copy_instr(instr);
      concat_TAC_instrs(&rebuilt, tac_instr_list(instr_copy));

      if (instr == node->body.last) {
        break;
      }
      instr = instr->next;
      if (instr == NULL) {
        fprintf(stderr,
                "CFG error: cannot rebuild basic block %u because its last "
                "instruction is not reachable from its body\n",
                i);
        exit(BCC_EXIT_INTERNAL);
      }
    }
  }

  return rebuilt;
}

// After removing blocks from the CFG, nodes may contain dangling edges. 
// Repair the CFG to maintain consistency. Removes empty blocks as a side effect.
struct CFG* repair_cfg(struct CFG* cfg){
  if (cfg == NULL) return NULL;

  // rebuild_body does not use CFG edges, 
  // so this is safe even if the CFG has dangling edges.
  struct TACInstrList instrs = rebuild_body(cfg);

  // instrs is now valid, so we can safely rebuild the CFG from it
  return build_cfg(instrs.head);
}
