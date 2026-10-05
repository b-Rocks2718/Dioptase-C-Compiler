#include "constant_fold.h"
#include "arena.h"
#include "TAC.h"
#include "const_eval.h"

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

// Describe whether constant folding keeps, replaces, or rejects an expression.
struct ConstantFoldResult {
  enum ConstantFoldAction action;
  struct TACInstr* replacement;
};

static struct ConstantFoldResult constant_fold_instr(struct TACInstr* instr);
static bool constant_cond_jump_result(const struct TACCondJump* jump,
                                      bool* result);

static struct ConstantFoldResult constant_fold_unchanged =  {
  CONSTANT_FOLD_UNCHANGED,
  NULL,
};

// Replace a constant-foldable expression with its computed value.
static struct ConstantFoldResult constant_fold_replacement(
    struct TACInstr* replacement) {
  if (replacement == NULL) {
    return constant_fold_unchanged;
  }
  return (struct ConstantFoldResult) {
    CONSTANT_FOLD_REPLACE,
    replacement,
  };
}

static struct ConstantFoldResult constant_fold_deletion = {
    CONSTANT_FOLD_DELETE,
    NULL
};

// Mask with the low `bits` bits set; used by Trunc/Extend, whose widths come
// from the instruction rather than from a type.
static uint64_t constant_mask(size_t bits) {
  return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
}

// Allocate a replacement instruction without exposing TAC.c internals.
static struct TACInstr* constant_instr_create(enum TACInstrType type) {
  struct TACInstr* instr = (struct TACInstr*)arena_alloc(sizeof(struct TACInstr));
  if (instr == NULL) {
    return NULL;
  }
  instr->type = type;
  instr->next = NULL;
  return instr;
}

// Allocate a typed constant value for a folded instruction.
// Operand Vals are shared by every later copy of the instruction, including
// the final TAC body, so they must outlive the optimizer's scratch arena.
static struct Val* constant_value_create(uint64_t value, struct Type* type) {
  uint64_t normalized;
  if (!const_normalize(value, type, &normalized)) {
    return NULL;
  }

  struct Val* val = (struct Val*)arena_alloc_persistent(sizeof(struct Val));
  if (val == NULL) {
    return NULL;
  }
  val->val_type = CONSTANT;
  val->val.const_value = normalized;
  val->type = type;
  return val;
}

// Replace a value-producing instruction with a typed constant copy
static struct TACInstr* constant_copy_create(struct Val* dst, uint64_t value) {
  if (dst == NULL) {
    return NULL;
  }
  struct Val* src = constant_value_create(value, dst->type);
  if (src == NULL) {
    return NULL;
  }
  struct TACInstr* copy = constant_instr_create(TACCOPY);
  if (copy == NULL) {
    return NULL;
  }
  copy->instr.tac_copy.dst = dst;
  copy->instr.tac_copy.src = src;
  return copy;
}

// Evaluate a conditional jump when both operands are constants of the same
// width. Returns false when the outcome cannot be decided at compile time.
static bool constant_cond_jump_result(const struct TACCondJump* jump,
                                      bool* result) {
  size_t left_bits;
  size_t right_bits;
  if (jump == NULL || result == NULL || jump->src1 == NULL || jump->src2 == NULL ||
      jump->src1->val_type != CONSTANT || jump->src2->val_type != CONSTANT ||
      !const_type_bits(jump->src1->type, &left_bits) ||
      !const_type_bits(jump->src2->type, &right_bits) || left_bits != right_bits) {
    return false;
  }
  return const_eval_condition(jump->condition, jump->src1->val.const_value,
                              jump->src1->type, jump->src2->val.const_value,
                              jump->src2->type, result) == CONST_EVAL_OK;
}

// Fold constant expressions throughout a TAC body.
struct TACInstrList constant_fold(struct TACInstrList body){
  struct TACInstr* prev = NULL;
  struct TACInstr* curr = body.head;
  while (curr != NULL) {
    struct TACInstr* next = curr->next;
    struct ConstantFoldResult fold = constant_fold_instr(curr);

    switch (fold.action) {
      case CONSTANT_FOLD_UNCHANGED:
        prev = curr;
        break;
      case CONSTANT_FOLD_REPLACE: {
        struct TACInstr* replacement = fold.replacement;
        if (prev == NULL) {
          body.head = replacement;
        } else {
          prev->next = replacement;
        }
        replacement->next = next;
        prev = replacement;
        break;
      }
      case CONSTANT_FOLD_DELETE:
        if (prev == NULL) {
          body.head = next;
        } else {
          prev->next = next;
        }
        break;
    }
    curr = next;
  }
  // prev is the last surviving instruction (NULL if everything was deleted)
  body.last = prev;
  return body;
}

// Decide how one instruction changes without mutating its containing list.
static struct ConstantFoldResult constant_fold_instr(struct TACInstr* instr) {
  if (instr == NULL) {
    return constant_fold_unchanged;
  }

  switch (instr->type) {
    case TACUNARY: {
      struct Val* src = instr->instr.tac_unary.src;
      struct Val* dst = instr->instr.tac_unary.dst;
      if (src == NULL || src->val_type != CONSTANT) {
        return constant_fold_unchanged;
      }

      // `!` tests the source at its own width; the other operators compute in
      // the destination type, which matches the source for them.
      enum UnOp op = instr->instr.tac_unary.op;
      const struct Type* eval_type = op == BOOL_NOT ? src->type : (dst == NULL ? NULL : dst->type);
      uint64_t value;
      if (const_eval_unary(op, src->val.const_value, eval_type, &value) != CONST_EVAL_OK) {
        return constant_fold_unchanged;
      }
      return constant_fold_replacement(constant_copy_create(dst, value));
    }
    case TACBINARY: {
      struct TACBinary* binary = &instr->instr.tac_binary;
      struct Val* left = binary->src1;
      struct Val* right = binary->src2;
      struct Type* result_type = binary->dst == NULL ? NULL : binary->dst->type;
      if (right == NULL) {
        return constant_fold_unchanged;
      }

      // ALU_MOV represents the comma operator. Earlier TAC has already
      // evaluated the left expression, and the result is the right operand.
      if (binary->alu_op == ALU_MOV) {
        struct TACInstr* copy = constant_instr_create(TACCOPY);
        if (copy == NULL) {
          return constant_fold_unchanged;
        }
        copy->instr.tac_copy.dst = binary->dst;
        copy->instr.tac_copy.src = right;
        return constant_fold_replacement(copy);
      }

      if (left == NULL || left->val_type != CONSTANT ||
          right->val_type != CONSTANT || result_type == NULL) {
        return constant_fold_unchanged;
      }

      // Undefined results (division by zero, INT_MIN / -1, bad shift counts)
      // are left for run time rather than folded.
      uint64_t value;
      if (const_eval_alu(binary->alu_op, left->val.const_value, left->type,
                         right->val.const_value, right->type, result_type,
                         &value) != CONST_EVAL_OK) {
        return constant_fold_unchanged;
      }
      return constant_fold_replacement(constant_copy_create(binary->dst, value));
    }
    case TACCOND_JUMP: {
      struct TACCondJump* jump = &instr->instr.tac_cond_jump;
      bool condition_result;
      if (!constant_cond_jump_result(jump, &condition_result)) {
        return constant_fold_unchanged;
      }
      if (!condition_result) {
        // A never-taken comparison has no remaining side effects.
        return constant_fold_deletion;
      }

      struct TACInstr* unconditional_jump = constant_instr_create(TACJUMP);
      if (unconditional_jump == NULL) {
        return constant_fold_unchanged;
      }
      unconditional_jump->instr.tac_jump.label = jump->label;
      return constant_fold_replacement(unconditional_jump);
    }
    case TACTRUNC: {
      struct TACTrunc* trunc = &instr->instr.tac_trunc;
      if (trunc->src == NULL || trunc->dst == NULL ||
          trunc->src->val_type != CONSTANT || trunc->target_size == 0 ||
          trunc->target_size > sizeof(uint64_t) || trunc->dst->type == NULL ||
          get_type_size(trunc->dst->type) != trunc->target_size) {
        return constant_fold_unchanged;
      }
      size_t target_bits = trunc->target_size * CHAR_BIT;
      uint64_t truncated = trunc->src->val.const_value & constant_mask(target_bits);
      return constant_fold_replacement(
          constant_copy_create(trunc->dst, truncated));
    }
    case TACEXTEND: {
      struct TACExtend* extend = &instr->instr.tac_extend;
      if (extend->src == NULL || extend->dst == NULL ||
          extend->src->val_type != CONSTANT || extend->src_size == 0 ||
          extend->src_size >= sizeof(uint64_t) || extend->src->type == NULL ||
          extend->dst->type == NULL ||
          get_type_size(extend->src->type) != extend->src_size ||
          get_type_size(extend->dst->type) <= extend->src_size) {
        return constant_fold_unchanged;
      }

      size_t src_bits = extend->src_size * CHAR_BIT;
      uint64_t src_mask = constant_mask(src_bits);
      uint64_t extended = extend->src->val.const_value & src_mask;
      uint64_t sign_bit = UINT64_C(1) << (src_bits - 1);
      if ((extended & sign_bit) != 0) {
        extended |= ~src_mask;
      }
      return constant_fold_replacement(
          constant_copy_create(extend->dst, extended));
    }
    default:
      return constant_fold_unchanged;
  }
}