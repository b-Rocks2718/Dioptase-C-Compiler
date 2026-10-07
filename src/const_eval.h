#ifndef CONST_EVAL_H
#define CONST_EVAL_H

#include "TAC.h"

#include <stdbool.h>
#include <stdint.h>

// Integer arithmetic with the target's C semantics, shared by every place the
// compiler computes a value instead of emitting code for it: the type checker's
// constant-expression evaluator, the constant-folding pass, and the TAC
// interpreter. Keeping one implementation guarantees that folding a constant at
// compile time and computing it at run time agree.
//
// Values are uint64_t bit patterns. A value is "normalized" to a type when it is
// truncated to the type's width and then sign-extended (signed types) or
// zero-extended (unsigned types and pointers) back to 64 bits. Every function
// here returns normalized results.

// Outcome of evaluating one operation. Anything other than CONST_EVAL_OK means
// the result is not defined by C (or not representable here), so a compile-time
// evaluator must not fold it and an interpreter must report it.
enum ConstEvalStatus {
  CONST_EVAL_OK,
  CONST_EVAL_DIV_BY_ZERO,
  // Signed division or remainder of the minimum value by -1 overflows.
  CONST_EVAL_SIGNED_OVERFLOW,
  // Negative shift count, or a count at least as large as the operand width.
  CONST_EVAL_BAD_SHIFT,
  // The type has no integer width (void, aggregates, wider than 64 bits), or
  // the operator is not an arithmetic one.
  CONST_EVAL_UNSUPPORTED,
};

// Width in bits of an integer or pointer type; false for any other type.
bool const_type_bits(const struct Type* type, size_t* bits);

// Normalize value to type (see the file comment). False if type has no integer width.
bool const_normalize(uint64_t value, const struct Type* type, uint64_t* out);

// Apply an ALU operation and produce a value of result_type. Each operand is
// interpreted in its own type (NULL means result_type), and the operation is
// carried out at the widest of the result and operand widths, never narrower
// than int: C's integer promotions and usual arithmetic conversions, which TAC
// leaves implicit by typing an instruction with its destination type. For
// shifts, right_type is the count's type and does not widen the operation.
enum ConstEvalStatus const_eval_alu(enum ALUOp op,
                                   uint64_t left, const struct Type* left_type,
                                   uint64_t right, const struct Type* right_type,
                                   const struct Type* result_type, uint64_t* out);

// Apply a unary operator to an operand of type `type`. BOOL_NOT yields 0 or 1;
// the other operators yield a value of `type`.
enum ConstEvalStatus const_eval_unary(enum UnOp op, uint64_t value,
                                     const struct Type* type, uint64_t* out);

// Evaluate a TAC comparison. Each operand is interpreted in its own type and
// the comparison is made at the wider of the two widths; the condition itself
// (CondG vs CondA) decides signed versus unsigned ordering.
enum ConstEvalStatus const_eval_condition(enum TACCondition cond, uint64_t left,
                                         const struct Type* left_type, uint64_t right,
                                         const struct Type* right_type, bool* out);

// Map an AST arithmetic or bitwise operator to the ALU operation that
// implements it for operands of `type` (signedness picks SDIV vs UDIV, etc.).
// COMMA_OP maps to ALU_MOV. False for operators with no ALU equivalent.
bool binop_alu_op(enum BinOp op, const struct Type* type, enum ALUOp* out);

// Map an AST relational operator to the TAC condition that tests it for
// operands of `type`. False for non-relational operators.
bool binop_condition(enum BinOp op, const struct Type* type, enum TACCondition* out);

#endif // CONST_EVAL_H
