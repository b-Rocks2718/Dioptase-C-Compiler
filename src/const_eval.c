#include "const_eval.h"

#include <limits.h>

// Mask with the low `bits` bits set (bits in 1..64).
static uint64_t low_mask(size_t bits) {
  return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
}

// Interpret the low `bits` bits of value as a two's-complement integer without
// relying on implementation-defined unsigned-to-signed conversion.
static int64_t signed_at_width(uint64_t value, size_t bits) {
  uint64_t mask = low_mask(bits);
  uint64_t truncated = value & mask;
  if ((truncated & (UINT64_C(1) << (bits - 1))) == 0) {
    return (int64_t)truncated;
  }
  uint64_t magnitude = ((~truncated) & mask) + UINT64_C(1);
  return magnitude == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)magnitude;
}

bool const_type_bits(const struct Type* type, size_t* bits) {
  if (type == NULL) {
    return false;
  }
  struct Type* t = (struct Type*)type;
  if (!is_arithmetic_type(t) && !is_pointer_type(t)) {
    return false;
  }
  size_t bytes = get_type_size(t);
  if (bytes == 0 || bytes > sizeof(uint64_t)) {
    return false;
  }
  *bits = bytes * CHAR_BIT;
  return true;
}

bool const_normalize(uint64_t value, const struct Type* type, uint64_t* out) {
  size_t bits;
  if (!const_type_bits(type, &bits)) {
    return false;
  }
  if (is_signed_type((struct Type*)type)) {
    *out = (uint64_t)signed_at_width(value, bits);
  } else {
    *out = value & low_mask(bits);
  }
  return true;
}

// Width at which arithmetic on `bits`-wide operands is carried out. C promotes
// operands narrower than int to int (C11 6.3.1.1), and TAC types narrow
// operations by their destination (a compound `c <<= 10` on a char is a char
// TAC shift), so narrow operations are evaluated at int width and only the
// final result is narrowed.
static size_t promoted_bits(size_t bits) {
  struct Type int_type = { .type = INT_TYPE };
  size_t int_bits = get_type_size(&int_type) * CHAR_BIT;
  return bits < int_bits ? int_bits : bits;
}

// Validate a shift count of type shift_type against the shifted operand's width.
static enum ConstEvalStatus shift_count(uint64_t right, const struct Type* shift_type,
                                        size_t width, uint64_t* out) {
  size_t count_bits;
  if (!const_type_bits(shift_type, &count_bits)) {
    return CONST_EVAL_UNSUPPORTED;
  }
  uint64_t count = right & low_mask(count_bits);
  if (is_signed_type((struct Type*)shift_type)) {
    int64_t signed_count = signed_at_width(right, count_bits);
    if (signed_count < 0) {
      return CONST_EVAL_BAD_SHIFT;
    }
    count = (uint64_t)signed_count;
  }
  if (count >= width) {
    return CONST_EVAL_BAD_SHIFT;
  }
  *out = count;
  return CONST_EVAL_OK;
}

// Shift a normalized operand of the given width; ASR replicates the sign bit.
static uint64_t shift_value(enum ALUOp op, uint64_t value, uint64_t count, size_t bits) {
  if (op == ALU_LSL || op == ALU_ASL) {
    return value << count;
  }
  uint64_t truncated = value & low_mask(bits);
  uint64_t result = truncated >> count;
  if (op == ALU_ASR && count != 0 && (truncated & (UINT64_C(1) << (bits - 1))) != 0) {
    result |= low_mask(bits) ^ (low_mask(bits) >> count);
  }
  return result;
}

enum ConstEvalStatus const_eval_alu(enum ALUOp op,
                                   uint64_t left, const struct Type* left_type,
                                   uint64_t right, const struct Type* right_type,
                                   const struct Type* result_type, uint64_t* out) {
  if (left_type == NULL) {
    left_type = result_type;
  }
  if (right_type == NULL) {
    right_type = result_type;
  }
  size_t result_bits;
  size_t left_bits;
  size_t right_bits;
  uint64_t l;
  if (!const_type_bits(result_type, &result_bits) ||
      !const_type_bits(left_type, &left_bits) ||
      !const_type_bits(right_type, &right_bits) ||
      !const_normalize(left, left_type, &l)) {
    return CONST_EVAL_UNSUPPORTED;
  }
  bool is_shift = op == ALU_LSL || op == ALU_ASL || op == ALU_LSR || op == ALU_ASR;
  // Evaluate at the widest participating width (never narrower than int).
  // TAC types an operation by its destination, but C performs it in the
  // operands' common type: `int /= long` divides in long, and `c <<= 10` on a
  // char shifts in int. A shift count's type does not widen the shift.
  size_t bits = result_bits > left_bits ? result_bits : left_bits;
  if (!is_shift && right_bits > bits) {
    bits = right_bits;
  }
  bits = promoted_bits(bits);

  uint64_t result;
  if (op == ALU_LSL || op == ALU_ASL || op == ALU_LSR || op == ALU_ASR) {
    uint64_t count;
    enum ConstEvalStatus status = shift_count(right, right_type, bits, &count);
    if (status != CONST_EVAL_OK) {
      return status;
    }
    result = shift_value(op, l, count, bits);
  } else {
    uint64_t r;
    if (!const_normalize(right, right_type, &r)) {
      return CONST_EVAL_UNSUPPORTED;
    }
    switch (op) {
      case ALU_ADD:
        result = l + r;
        break;
      case ALU_SUB:
        result = l - r;
        break;
      case ALU_SMUL:
      case ALU_UMUL:
        // The low product bits are the same for signed and unsigned operands,
        // and unsigned host arithmetic wraps without undefined behavior.
        result = l * r;
        break;
      case ALU_SDIV:
      case ALU_SMOD: {
        int64_t sl = signed_at_width(l, bits);
        int64_t sr = signed_at_width(r, bits);
        if (sr == 0) {
          return CONST_EVAL_DIV_BY_ZERO;
        }
        if (sr == -1 && sl == signed_at_width(UINT64_C(1) << (bits - 1), bits)) {
          return CONST_EVAL_SIGNED_OVERFLOW;
        }
        result = (uint64_t)(op == ALU_SDIV ? sl / sr : sl % sr);
        break;
      }
      case ALU_UDIV:
      case ALU_UMOD: {
        uint64_t ul = l & low_mask(bits);
        uint64_t ur = r & low_mask(bits);
        if (ur == 0) {
          return CONST_EVAL_DIV_BY_ZERO;
        }
        result = op == ALU_UDIV ? ul / ur : ul % ur;
        break;
      }
      case ALU_AND:
        result = l & r;
        break;
      case ALU_OR:
        result = l | r;
        break;
      case ALU_XOR:
        result = l ^ r;
        break;
      case ALU_MOV:
        result = r;
        break;
      default:
        return CONST_EVAL_UNSUPPORTED;
    }
  }
  return const_normalize(result, result_type, out) ? CONST_EVAL_OK : CONST_EVAL_UNSUPPORTED;
}

enum ConstEvalStatus const_eval_unary(enum UnOp op, uint64_t value,
                                     const struct Type* type, uint64_t* out) {
  uint64_t v;
  if (!const_normalize(value, type, &v)) {
    return CONST_EVAL_UNSUPPORTED;
  }
  switch (op) {
    case COMPLEMENT:
      v = ~v;
      break;
    case NEGATE:
      // Unsigned subtraction gives the two's-complement bits without host
      // signed-overflow undefined behavior for the minimum value.
      v = UINT64_C(0) - v;
      break;
    case UNARY_PLUS:
      break;
    case BOOL_NOT:
      *out = v == 0;
      return CONST_EVAL_OK;
    default:
      return CONST_EVAL_UNSUPPORTED;
  }
  return const_normalize(v, type, out) ? CONST_EVAL_OK : CONST_EVAL_UNSUPPORTED;
}

enum ConstEvalStatus const_eval_condition(enum TACCondition cond, uint64_t left,
                                         const struct Type* left_type, uint64_t right,
                                         const struct Type* right_type, bool* out) {
  // TAC may compare operands of different widths when a widening conversion
  // needed no instruction (unsigned int against unsigned long), so compare at
  // the wider width. Each operand is normalized to its own type first.
  size_t left_bits;
  size_t right_bits;
  if (!const_type_bits(left_type, &left_bits) || !const_type_bits(right_type, &right_bits) ||
      !const_normalize(left, left_type, &left) || !const_normalize(right, right_type, &right)) {
    return CONST_EVAL_UNSUPPORTED;
  }
  size_t bits = left_bits > right_bits ? left_bits : right_bits;
  // The condition, not the type, decides signedness: TAC picks CondG vs CondA
  // from the operand type when it lowers the comparison.
  uint64_t ul = left & low_mask(bits);
  uint64_t ur = right & low_mask(bits);
  int64_t sl = signed_at_width(left, bits);
  int64_t sr = signed_at_width(right, bits);
  switch (cond) {
    case CondE:  *out = ul == ur; return CONST_EVAL_OK;
    case CondNE: *out = ul != ur; return CONST_EVAL_OK;
    case CondA:  *out = ul > ur;  return CONST_EVAL_OK;
    case CondAE: *out = ul >= ur; return CONST_EVAL_OK;
    case CondB:  *out = ul < ur;  return CONST_EVAL_OK;
    case CondBE: *out = ul <= ur; return CONST_EVAL_OK;
    case CondG:  *out = sl > sr;  return CONST_EVAL_OK;
    case CondGE: *out = sl >= sr; return CONST_EVAL_OK;
    case CondL:  *out = sl < sr;  return CONST_EVAL_OK;
    case CondLE: *out = sl <= sr; return CONST_EVAL_OK;
  }
  return CONST_EVAL_UNSUPPORTED;
}

bool binop_alu_op(enum BinOp op, const struct Type* type, enum ALUOp* out) {
  bool is_signed = is_signed_type((struct Type*)type);
  switch (op) {
    case ADD_OP:  *out = ALU_ADD; return true;
    case SUB_OP:  *out = ALU_SUB; return true;
    case MUL_OP:  *out = is_signed ? ALU_SMUL : ALU_UMUL; return true;
    case DIV_OP:  *out = is_signed ? ALU_SDIV : ALU_UDIV; return true;
    case MOD_OP:  *out = is_signed ? ALU_SMOD : ALU_UMOD; return true;
    case BIT_AND: *out = ALU_AND; return true;
    case BIT_OR:  *out = ALU_OR; return true;
    case BIT_XOR: *out = ALU_XOR; return true;
    case BIT_SHL: *out = is_signed ? ALU_ASL : ALU_LSL; return true;
    case BIT_SHR: *out = is_signed ? ALU_ASR : ALU_LSR; return true;
    case COMMA_OP: *out = ALU_MOV; return true;
    default:
      return false;
  }
}

bool binop_condition(enum BinOp op, const struct Type* type, enum TACCondition* out) {
  bool is_signed = is_signed_type((struct Type*)type);
  switch (op) {
    case BOOL_EQ:  *out = CondE; return true;
    case BOOL_NEQ: *out = CondNE; return true;
    case BOOL_GE:  *out = is_signed ? CondG : CondA; return true;
    case BOOL_GEQ: *out = is_signed ? CondGE : CondAE; return true;
    case BOOL_LE:  *out = is_signed ? CondL : CondB; return true;
    case BOOL_LEQ: *out = is_signed ? CondLE : CondBE; return true;
    default:
      return false;
  }
}
