#include "asm_cfg.h"
#include "exit_codes.h"

#include <stdio.h>
#include <stdlib.h>

// Next ASM instruction in a function body.
static const void* asm_next(const void* instr) {
  return ((const struct AsmInstr*)instr)->next;
}

// Control-flow role of an ASM instruction. Calls fall through: they return to
// the next instruction, so they do not end a block.
static enum CFGInstrKind asm_classify(const void* opaque_instr, const struct Slice** label) {
  const struct AsmInstr* instr = opaque_instr;
  switch (instr->type) {
    case ASM_LABEL:
      *label = instr->instr.asm_label.label;
      return CFG_INSTR_LABEL;
    case ASM_JUMP:
      *label = instr->instr.asm_jump.label;
      return CFG_INSTR_JUMP;
    case ASM_COND_JUMP:
      *label = instr->instr.asm_cond_jump.label;
      return CFG_INSTR_COND_JUMP;
    case ASM_RET:
      return CFG_INSTR_RETURN;
    case ASM_TAIL_CALL:
    case ASM_TAIL_CALL_INDIRECT:
      return CFG_INSTR_TAIL_CALL;
    default:
      return CFG_INSTR_OTHER;
  }
}

// Extend a block's range to end at instr. The builder appends a function's
// instructions in list order, so each block stays a contiguous range. The
// const is cast away because the CFG refers to the function's own
// instructions, which later passes rewrite in place.
static void asm_append(struct CFGNode* block, const void* opaque_instr) {
  struct AsmInstr* instr = (struct AsmInstr*)opaque_instr;
  if (block->type != CFG_BASIC_BLOCK) {
    fprintf(stderr,
            "ASM CFG error: cannot append ASM instruction type %d to CFG node type %d; "
            "expected a basic block\n",
            (int)instr->type, (int)block->type);
    exit(BCC_EXIT_INTERNAL);
  }
  if (block->asm_head == NULL) {
    block->asm_head = instr;
  }
  block->asm_last = instr;
}

// First instruction of an ASM block, or NULL.
static const void* asm_block_first(const struct CFGNode* node) {
  return node->asm_head;
}

// Last instruction of an ASM block, or NULL.
static const void* asm_block_last(const struct CFGNode* node) {
  return node->asm_last;
}

// Print an ASM block's instructions.
static void asm_print_block(const struct CFGNode* node) {
  for (const struct AsmInstr* instr = node->asm_head; instr != NULL; instr = instr->next) {
    print_asm_instr(instr, 1);
    if (instr == node->asm_last) {
      break;
    }
  }
}

// How the shared CFG builder and printer in cfg.c see ASM instructions.
static const struct CFGInstrOps kAsmCFGOps = {
  .next = asm_next,
  .classify = asm_classify,
  .append = asm_append,
  .block_first = asm_block_first,
  .block_last = asm_block_last,
  .print_block = asm_print_block,
};

struct CFG* build_asm_cfg(struct AsmInstr* instrs) {
  return build_cfg_with(instrs, &kAsmCFGOps);
}

void print_asm_cfg(const struct CFG* cfg) {
  print_cfg_with(cfg, &kAsmCFGOps);
}
