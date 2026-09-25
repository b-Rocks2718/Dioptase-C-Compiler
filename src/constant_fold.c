#include "constant_fold.h"
#include "arena.h"
#include "TAC.h"

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

// Return the integer width represented by a TAC type.
static bool constant_width_bits(const struct Type* type, size_t* bits) {
  if (type == NULL || bits == NULL) {
    return false;
  }
  size_t bytes = get_type_size((struct Type*)type);
  if (bytes == 0 || bytes > sizeof(uint64_t)) {
    return false;
  }
  *bits = bytes * CHAR_BIT;
  return true;
}

// Build a mask containing the requested number of low-order 1's
static uint64_t constant_mask(size_t bits) {
  return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
}

// Normalize raw constant bits to the representation of a TAC type.
static bool normalize_constant(uint64_t value, const struct Type* type,
                               uint64_t* result) {
  size_t bits;
  if (result == NULL || !constant_width_bits(type, &bits)) {
    return false;
  }

  // mask to only `bits` low-order bits, then sign-extend if the type is signed
  uint64_t mask = constant_mask(bits);
  value &= mask;
  
  if (is_signed_type((struct Type*)type) && bits < 64) {
    uint64_t sign_bit = UINT64_C(1) << (bits - 1);
    if ((value & sign_bit) != 0) {
      value |= ~mask;
    }
  }
  *result = value;
  return true;
}

// Interpret raw constant bits as a signed integer of the given width.
static bool constant_as_signed(uint64_t value, const struct Type* type,
                               int64_t* result) {
  size_t bits;
  if (result == NULL || !constant_width_bits(type, &bits)) {
    return false;
  }

  uint64_t mask = constant_mask(bits);
  uint64_t truncated = value & mask;
  uint64_t sign_bit = UINT64_C(1) << (bits - 1);
  if ((truncated & sign_bit) == 0) {
    *result = (int64_t)truncated;
    return true;
  }

  uint64_t magnitude = ((~truncated) & mask) + UINT64_C(1);
  if (magnitude == (UINT64_C(1) << 63)) {
    *result = INT64_MIN;
  } else {
    *result = -(int64_t)magnitude;
  }
  return true;
}

// Allocate a replacement instruction without exposing TAC.c internals.
static struct TACInstr* constant_instr_create(enum TACInstrType type) {
  struct TACInstr* instr = (struct TACInstr*)arena_alloc(sizeof(struct TACInstr));
  if (instr == NULL) {
    return NULL;
  }
  instr->type = type;
  instr->reaching_copies.head = NULL;
  instr->reaching_copies.last = NULL;
  instr->live_vars.head = NULL;
  instr->live_vars.last = NULL;
  instr->next = NULL;
  return instr;
}

// Allocate a typed constant value for a folded instruction.
static struct Val* constant_value_create(uint64_t value, struct Type* type) {
  uint64_t normalized;
  if (!normalize_constant(value, type, &normalized)) {
    return NULL;
  }

  struct Val* val = (struct Val*)arena_alloc(sizeof(struct Val));
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

// Evaluate a conditional jump when both operands are constants.
// Return value indicates whether the jump condition can be determined at compile time
static bool constant_cond_jump_result(const struct TACCondJump* jump,
                                      bool* result) {
  // Validate inputs, ensure jump args are constants
  if (jump == NULL || result == NULL || jump->src1 == NULL || jump->src2 == NULL ||
      jump->src1->val_type != CONSTANT || jump->src2->val_type != CONSTANT ||
      jump->src1->type == NULL || jump->src2->type == NULL) {
    return false;
  }

  // ensure both operands have the same bit width
  size_t left_bits;
  size_t right_bits;
  if (!constant_width_bits(jump->src1->type, &left_bits) ||
      !constant_width_bits(jump->src2->type, &right_bits) ||
      left_bits != right_bits) {
    return false;
  }

  uint64_t mask = constant_mask(left_bits);
  uint64_t unsigned_left = jump->src1->val.const_value & mask;
  uint64_t unsigned_right = jump->src2->val.const_value & mask;
  int64_t signed_left;
  int64_t signed_right;

  // determine whether the jump will be taken
  switch (jump->condition) {
    case CondE:
      *result = unsigned_left == unsigned_right;
      return true;
    case CondNE:
      *result = unsigned_left != unsigned_right;
      return true;
    case CondG:
    case CondGE:
    case CondL:
    case CondLE:
      if (!constant_as_signed(jump->src1->val.const_value, jump->src1->type,
                              &signed_left) ||
          !constant_as_signed(jump->src2->val.const_value, jump->src2->type,
                              &signed_right)) {
        return false;
      }
      if (jump->condition == CondG) {
        *result = signed_left > signed_right;
      } else if (jump->condition == CondGE) {
        *result = signed_left >= signed_right;
      } else if (jump->condition == CondL) {
        *result = signed_left < signed_right;
      } else {
        *result = signed_left <= signed_right;
      }
      return true;
    case CondA:
      *result = unsigned_left > unsigned_right;
      return true;
    case CondAE:
      *result = unsigned_left >= unsigned_right;
      return true;
    case CondB:
      *result = unsigned_left < unsigned_right;
      return true;
    case CondBE:
      *result = unsigned_left <= unsigned_right;
      return true;
  }
  return false;
}

// Evaluate an arithmetic right shift without relying on the host's
static bool constant_arithmetic_shift_right(uint64_t value,
                                            uint64_t shift,
                                            const struct Type* type,
                                            uint64_t* result) {
  size_t bits;
  if (result == NULL || !constant_width_bits(type, &bits) || shift >= bits) {
    return false;
  }

  uint64_t mask = constant_mask(bits);
  uint64_t truncated = value & mask;
  uint64_t shifted = truncated >> shift;
  uint64_t sign_bit = UINT64_C(1) << (bits - 1);
  if (shift != 0 && (truncated & sign_bit) != 0) {
    shifted |= mask ^ (mask >> shift);
  }
  return normalize_constant(shifted, type, result);
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

      switch (instr->instr.tac_unary.op) {
        case COMPLEMENT:
          return constant_fold_replacement(
              constant_copy_create(dst, ~src->val.const_value));
        case NEGATE:
          // Unsigned subtraction computes the same two's-complement result bits
          // without host signed-overflow UB for the minimum signed value.
          return constant_fold_replacement(constant_copy_create(
              dst, UINT64_C(0) - src->val.const_value));
        case BOOL_NOT: {
          uint64_t normalized;
          if (!normalize_constant(src->val.const_value, src->type, &normalized)) {
            return constant_fold_unchanged;
          }
          return constant_fold_replacement(
              constant_copy_create(dst, normalized == 0));
        }
        case UNARY_PLUS:
          return constant_fold_replacement(
              constant_copy_create(dst, src->val.const_value));
      }
      return constant_fold_unchanged;
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

      uint64_t unsigned_left;
      uint64_t unsigned_right;
      if (!normalize_constant(left->val.const_value, result_type, &unsigned_left) ||
          !normalize_constant(right->val.const_value, result_type, &unsigned_right)) {
        return constant_fold_unchanged;
      }

      switch (binary->alu_op) {
        case ALU_ADD:
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left + unsigned_right));
        case ALU_SUB:
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left - unsigned_right));
        case ALU_SMUL:
        case ALU_UMUL:
          // Low two's-complement product bits are identical for signed and
          // unsigned multiplication; unsigned arithmetic has defined wrap.
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left * unsigned_right));
        case ALU_SDIV:
        case ALU_SMOD: {
          int64_t signed_left;
          int64_t signed_right;
          size_t result_bits;
          if (!constant_as_signed(left->val.const_value, result_type, &signed_left) ||
              !constant_as_signed(right->val.const_value, result_type, &signed_right) ||
              !constant_width_bits(result_type, &result_bits) || signed_right == 0) {
            return constant_fold_unchanged;
          }
          int64_t minimum = result_bits == 64
                                ? INT64_MIN
                                : -(INT64_C(1) << (result_bits - 1));
          if (signed_left == minimum && signed_right == -1) {
            return constant_fold_unchanged;
          }
          int64_t signed_result = binary->alu_op == ALU_SDIV
                                      ? signed_left / signed_right
                                      : signed_left % signed_right;
          return constant_fold_replacement(
              constant_copy_create(binary->dst, (uint64_t)signed_result));
        }
        case ALU_UDIV:
          if (unsigned_right == 0) {
            return constant_fold_unchanged;
          }
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left / unsigned_right));
        case ALU_UMOD:
          if (unsigned_right == 0) {
            return constant_fold_unchanged;
          }
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left % unsigned_right));
        case ALU_AND:
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left & unsigned_right));
        case ALU_OR:
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left | unsigned_right));
        case ALU_XOR:
          return constant_fold_replacement(constant_copy_create(
              binary->dst, unsigned_left ^ unsigned_right));
        case ALU_LSL:
        case ALU_LSR:
        case ALU_ASL:
        case ALU_ASR: {
          uint64_t shift;
          if (right->type != NULL && is_signed_type(right->type)) {
            int64_t signed_shift;
            if (!constant_as_signed(right->val.const_value, right->type, &signed_shift) ||
                signed_shift < 0) {
              return constant_fold_unchanged;
            }
            shift = (uint64_t)signed_shift;
          } else if (!normalize_constant(right->val.const_value, right->type, &shift)) {
            return constant_fold_unchanged;
          }

          size_t result_bits;
          if (!constant_width_bits(result_type, &result_bits) || shift >= result_bits) {
            return constant_fold_unchanged;
          }
          if (binary->alu_op == ALU_LSL || binary->alu_op == ALU_ASL) {
            return constant_fold_replacement(
                constant_copy_create(binary->dst, unsigned_left << shift));
          }
          if (binary->alu_op == ALU_LSR) {
            return constant_fold_replacement(
                constant_copy_create(binary->dst, unsigned_left >> shift));
          }

          uint64_t shifted;
          if (!constant_arithmetic_shift_right(left->val.const_value, shift,
                                               result_type, &shifted)) {
            return constant_fold_unchanged;
          }
          return constant_fold_replacement(
              constant_copy_create(binary->dst, shifted));
        }
        case ALU_MOV:
          return constant_fold_unchanged;
      }
      return constant_fold_unchanged;
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