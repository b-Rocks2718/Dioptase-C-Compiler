#include "optimization.h"
#include "arena.h"

#include <limits.h>
#include <stdint.h>

static struct TACInstr* constant_fold_instr(struct TACInstr* instr);

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
  instr->next = NULL;
  instr->last = instr;
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

// iterate over each function and optimize its body
void optimize(struct TACProg* prog, struct OptimizationOptions options) {
  if (prog == NULL) {
    return;
  }
  for (struct TopLevel* top = prog->head; top != NULL; top = top->next) {
    if (top->type == FUNC) {
      struct TACInstr* body = top->body;
      top->body = optimize_body(body, options);
    }
  }
}

struct TACInstr* optimize_body(struct TACInstr* body, struct OptimizationOptions options) {
  if (body == NULL) {
    return body;
  }

  while (true) {
    
    struct TACInstr* post_const_fold_body = body;
    if (options.constant_fold) {
      post_const_fold_body = constant_fold(body);
    } 

    /*
    struct CFG* cfg = build_cfg(post_const_fold_body);

    if (options.dead_code_elim) {
      cfg = dead_code_elim(cfg);
    }

    if (options.copy_prop){
      cfg = copy_prop(cfg);
    }

    if (options.dead_store_elim) {
      cfg = dead_store_elim(cfg);
    }
    */

    struct TACInstr* new_body = post_const_fold_body; // rebuild_body(cfg);
    if (compare_bodies(new_body, body)) {
      return new_body;
    }
    body = new_body;
  }
}

struct TACInstr* constant_fold(struct TACInstr* body){
  struct TACInstr* prev = NULL;
  struct TACInstr* curr = body;
  while (curr != NULL) {
    struct TACInstr* new_instr = constant_fold_instr(curr);
    if (new_instr != NULL) {
      // instruction changed by constant folding, replace it in the list
      if (prev == NULL) {
        new_instr->last = curr->last == curr ? new_instr : curr->last;
        body = new_instr;
      } else {
        prev->next = new_instr;
      }
      new_instr->next = curr->next;
      prev = new_instr;
    } else {
      // no change, move to next instruction
      prev = curr;
    }
    curr = curr->next;
  }
  return body;
}

// Replace a constant value-producing instruction with TACCOPY.
static struct TACInstr* constant_fold_instr(struct TACInstr* instr) {
  if (instr == NULL) {
    return NULL;
  }

  switch (instr->type) {
    case TACUNARY: {
      struct Val* src = instr->instr.tac_unary.src;
      struct Val* dst = instr->instr.tac_unary.dst;
      if (src == NULL || src->val_type != CONSTANT) {
        return NULL;
      }

      switch (instr->instr.tac_unary.op) {
        case COMPLEMENT:
          return constant_copy_create(dst, ~src->val.const_value);
        case NEGATE:
          // Unsigned subtraction computes the same two's-complement result bits
          // without host signed-overflow UB for the minimum signed value.
          return constant_copy_create(dst, UINT64_C(0) - src->val.const_value);
        case BOOL_NOT: {
          uint64_t normalized;
          if (!normalize_constant(src->val.const_value, src->type, &normalized)) {
            return NULL;
          }
          return constant_copy_create(dst, normalized == 0);
        }
        case UNARY_PLUS:
          return constant_copy_create(dst, src->val.const_value);
      }
      return NULL;
    }
    case TACBINARY: {
      struct TACBinary* binary = &instr->instr.tac_binary;
      struct Val* left = binary->src1;
      struct Val* right = binary->src2;
      struct Type* result_type = binary->dst == NULL ? NULL : binary->dst->type;
      if (right == NULL) {
        return NULL;
      }

      // ALU_MOV represents the comma operator. Earlier TAC has already
      // evaluated the left expression, and the result is the right operand.
      if (binary->alu_op == ALU_MOV) {
        struct TACInstr* copy = constant_instr_create(TACCOPY);
        if (copy == NULL) {
          return NULL;
        }
        copy->instr.tac_copy.dst = binary->dst;
        copy->instr.tac_copy.src = right;
        return copy;
      }

      if (left == NULL || left->val_type != CONSTANT ||
          right->val_type != CONSTANT || result_type == NULL) {
        return NULL;
      }

      uint64_t unsigned_left;
      uint64_t unsigned_right;
      if (!normalize_constant(left->val.const_value, result_type, &unsigned_left) ||
          !normalize_constant(right->val.const_value, result_type, &unsigned_right)) {
        return NULL;
      }

      switch (binary->alu_op) {
        case ALU_ADD:
          return constant_copy_create(binary->dst, unsigned_left + unsigned_right);
        case ALU_SUB:
          return constant_copy_create(binary->dst, unsigned_left - unsigned_right);
        case ALU_SMUL:
        case ALU_UMUL:
          // Low two's-complement product bits are identical for signed and
          // unsigned multiplication; unsigned arithmetic has defined wrap.
          return constant_copy_create(binary->dst, unsigned_left * unsigned_right);
        case ALU_SDIV:
        case ALU_SMOD: {
          int64_t signed_left;
          int64_t signed_right;
          size_t result_bits;
          if (!constant_as_signed(left->val.const_value, result_type, &signed_left) ||
              !constant_as_signed(right->val.const_value, result_type, &signed_right) ||
              !constant_width_bits(result_type, &result_bits) || signed_right == 0) {
            return NULL;
          }
          int64_t minimum = result_bits == 64
                                ? INT64_MIN
                                : -(INT64_C(1) << (result_bits - 1));
          if (signed_left == minimum && signed_right == -1) {
            return NULL;
          }
          int64_t signed_result = binary->alu_op == ALU_SDIV
                                      ? signed_left / signed_right
                                      : signed_left % signed_right;
          return constant_copy_create(binary->dst, (uint64_t)signed_result);
        }
        case ALU_UDIV:
          if (unsigned_right == 0) {
            return NULL;
          }
          return constant_copy_create(binary->dst, unsigned_left / unsigned_right);
        case ALU_UMOD:
          if (unsigned_right == 0) {
            return NULL;
          }
          return constant_copy_create(binary->dst, unsigned_left % unsigned_right);
        case ALU_AND:
          return constant_copy_create(binary->dst, unsigned_left & unsigned_right);
        case ALU_OR:
          return constant_copy_create(binary->dst, unsigned_left | unsigned_right);
        case ALU_XOR:
          return constant_copy_create(binary->dst, unsigned_left ^ unsigned_right);
        case ALU_LSL:
        case ALU_LSR:
        case ALU_ASL:
        case ALU_ASR: {
          uint64_t shift;
          if (right->type != NULL && is_signed_type(right->type)) {
            int64_t signed_shift;
            if (!constant_as_signed(right->val.const_value, right->type, &signed_shift) ||
                signed_shift < 0) {
              return NULL;
            }
            shift = (uint64_t)signed_shift;
          } else if (!normalize_constant(right->val.const_value, right->type, &shift)) {
            return NULL;
          }

          size_t result_bits;
          if (!constant_width_bits(result_type, &result_bits) || shift >= result_bits) {
            return NULL;
          }
          if (binary->alu_op == ALU_LSL || binary->alu_op == ALU_ASL) {
            return constant_copy_create(binary->dst, unsigned_left << shift);
          }
          if (binary->alu_op == ALU_LSR) {
            return constant_copy_create(binary->dst, unsigned_left >> shift);
          }

          uint64_t shifted;
          if (!constant_arithmetic_shift_right(left->val.const_value, shift,
                                               result_type, &shifted)) {
            return NULL;
          }
          return constant_copy_create(binary->dst, shifted);
        }
        case ALU_MOV:
          return NULL;
      }
      return NULL;
    }
    case TACTRUNC: {
      struct TACTrunc* trunc = &instr->instr.tac_trunc;
      if (trunc->src == NULL || trunc->dst == NULL ||
          trunc->src->val_type != CONSTANT || trunc->target_size == 0 ||
          trunc->target_size > sizeof(uint64_t) || trunc->dst->type == NULL ||
          get_type_size(trunc->dst->type) != trunc->target_size) {
        return NULL;
      }
      size_t target_bits = trunc->target_size * CHAR_BIT;
      uint64_t truncated = trunc->src->val.const_value & constant_mask(target_bits);
      return constant_copy_create(trunc->dst, truncated);
    }
    case TACEXTEND: {
      struct TACExtend* extend = &instr->instr.tac_extend;
      if (extend->src == NULL || extend->dst == NULL ||
          extend->src->val_type != CONSTANT || extend->src_size == 0 ||
          extend->src_size >= sizeof(uint64_t) || extend->src->type == NULL ||
          extend->dst->type == NULL ||
          get_type_size(extend->src->type) != extend->src_size ||
          get_type_size(extend->dst->type) <= extend->src_size) {
        return NULL;
      }

      size_t src_bits = extend->src_size * CHAR_BIT;
      uint64_t src_mask = constant_mask(src_bits);
      uint64_t extended = extend->src->val.const_value & src_mask;
      uint64_t sign_bit = UINT64_C(1) << (src_bits - 1);
      if ((extended & sign_bit) != 0) {
        extended |= ~src_mask;
      }
      return constant_copy_create(extend->dst, extended);
    }
    default:
      return NULL;
  }
}

struct CFG* dead_code_elim(struct CFG* cfg){
  // TODO
  return NULL;
}

struct CFG* copy_prop(struct CFG* cfg){
  // TODO
  return NULL;
}

struct CFG* dead_store_elim(struct CFG* cfg){
  // TODO
  return NULL;
}
