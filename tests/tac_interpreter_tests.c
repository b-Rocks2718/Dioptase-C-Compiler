#include "TAC.h"
#include "arena.h"
#include "optimization.h"
#include "slice.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// TAC interpreter written by Codex

static struct Type kTestIntType = { .type = INT_TYPE };
static struct Type kTestPtrType = {
  .type = POINTER_TYPE,
  .type_data.pointer_type = { .referenced_type = &kTestIntType },
};

// Purpose: Build a Slice from a string literal for test data.
// Inputs: text is a null-terminated C string.
// Outputs: Returns a Slice referencing the literal.
// Invariants/Assumptions: The literal outlives the test execution.
static struct Slice tac_slice_literal(const char* text) {
  struct Slice slice;
  slice.start = text;
  slice.len = strlen(text);
  return slice;
}

// Purpose: Build a constant TAC value for test data.
// Inputs: value is the integer literal to store; type describes its width/sign.
// Outputs: Returns a Val tagged as CONSTANT.
// Invariants/Assumptions: value is already in the host int range.
static struct Val tac_val_const(int value, struct Type* type) {
  struct Val val;
  val.val_type = CONSTANT;
  val.val.const_value = (uint64_t)(int64_t)value;
  val.type = type;
  return val;
}

// Purpose: Build a constant TAC value from an exact 64-bit bit pattern.
// Inputs: value is the raw integer representation; type supplies width/sign.
// Outputs: Returns a Val tagged as CONSTANT.
// Invariants/Assumptions: The caller chooses a value valid for the test type.
static struct Val tac_val_const_bits(uint64_t value, struct Type* type) {
  struct Val val;
  val.val_type = CONSTANT;
  val.val.const_value = value;
  val.type = type;
  return val;
}

// Purpose: Build a variable TAC value for test data.
// Inputs: name is the variable identifier slice; type describes its width/sign.
// Outputs: Returns a Val tagged as VARIABLE.
// Invariants/Assumptions: name must outlive the test execution.
static struct Val tac_val_var(struct Slice* name, struct Type* type) {
  struct Val val;
  val.val_type = VARIABLE;
  val.val.var_name = name;
  val.type = type;
  return val;
}

// Purpose: Initialize a TAC instruction node for tests.
// Inputs: instr is the node to initialize; type is the instruction type.
// Outputs: Clears the node and sets its type/links.
// Invariants/Assumptions: instr points to writable memory.
static void tac_init_instr(struct TACInstr* instr, enum TACInstrType type) {
  memset(instr, 0, sizeof(*instr));
  instr->type = type;
  instr->next = NULL;
  instr->last = instr;
}

// Purpose: Link two TAC instruction nodes in a test list.
// Inputs: first is the head; second is appended after first.
// Outputs: Updates first->next to second.
// Invariants/Assumptions: Caller maintains list ordering.
static void tac_link_instr(struct TACInstr* first, struct TACInstr* second) {
  first->next = second;
  first->last = second->last ? second->last : second;
}

// Purpose: Compare interpreter output against an expected value.
// Inputs: name is the test name; got/expected are the result values.
// Outputs: Returns true on success and prints a failure message otherwise.
// Invariants/Assumptions: Outputs are integer results from TAC interpretation.
static bool tac_expect_result(const char* name, int got, int expected) {
  if (got == expected) {
    return true;
  }
  printf("TAC interpreter test %s failed: expected %d, got %d\n", name, expected, got);
  return false;
}

/*
Purpose: Verify that a constant return from main is propagated.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 7.
Invariants/Assumptions: Exercises TACRETURN with a constant value.

Readable TAC:
  func main:
    return 7
*/
static bool tac_test_return_const(void) {
  const int kReturnValue = 7;
  struct Slice main_name = tac_slice_literal("main");
  struct Val ret_val = tac_val_const(kReturnValue, &kTestIntType);

  struct TACInstr ret_instr;
  tac_init_instr(&ret_instr, TACRETURN);
  ret_instr.instr.tac_return.dst = &ret_val;

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &ret_instr;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("return_const", result, kReturnValue);
}

/*
Purpose: Verify arithmetic and copies in main.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 14.
Invariants/Assumptions: Exercises TACCOPY and TACBINARY operators.

Readable TAC:
  func main:
    a = 3
    b = 4
    t0 = a + b
    t1 = t0 * 2
    return t1
*/
static bool tac_test_arithmetic(void) {
  const int kFirst = 3;
  const int kSecond = 4;
  const int kMultiplier = 2;
  const int kExpected = 14;
  struct Slice main_name = tac_slice_literal("main");
  struct Slice a_name = tac_slice_literal("a");
  struct Slice b_name = tac_slice_literal("b");
  struct Slice t0_name = tac_slice_literal("t0");
  struct Slice t1_name = tac_slice_literal("t1");

  struct Val a_val = tac_val_var(&a_name, &kTestIntType);
  struct Val b_val = tac_val_var(&b_name, &kTestIntType);
  struct Val t0_val = tac_val_var(&t0_name, &kTestIntType);
  struct Val t1_val = tac_val_var(&t1_name, &kTestIntType);
  struct Val const_3 = tac_val_const(kFirst, &kTestIntType);
  struct Val const_4 = tac_val_const(kSecond, &kTestIntType);
  struct Val const_2 = tac_val_const(kMultiplier, &kTestIntType);

  struct TACInstr copy_a;
  struct TACInstr copy_b;
  struct TACInstr add_instr;
  struct TACInstr mul_instr;
  struct TACInstr ret_instr;
  tac_init_instr(&copy_a, TACCOPY);
  tac_init_instr(&copy_b, TACCOPY);
  tac_init_instr(&add_instr, TACBINARY);
  tac_init_instr(&mul_instr, TACBINARY);
  tac_init_instr(&ret_instr, TACRETURN);

  copy_a.instr.tac_copy.dst = &a_val;
  copy_a.instr.tac_copy.src = &const_3;
  copy_b.instr.tac_copy.dst = &b_val;
  copy_b.instr.tac_copy.src = &const_4;

  add_instr.instr.tac_binary.alu_op = ALU_ADD;
  add_instr.instr.tac_binary.dst = &t0_val;
  add_instr.instr.tac_binary.src1 = &a_val;
  add_instr.instr.tac_binary.src2 = &b_val;

  mul_instr.instr.tac_binary.alu_op = ALU_SMUL;
  mul_instr.instr.tac_binary.dst = &t1_val;
  mul_instr.instr.tac_binary.src1 = &t0_val;
  mul_instr.instr.tac_binary.src2 = &const_2;

  ret_instr.instr.tac_return.dst = &t1_val;

  tac_link_instr(&copy_a, &copy_b);
  tac_link_instr(&copy_b, &add_instr);
  tac_link_instr(&add_instr, &mul_instr);
  tac_link_instr(&mul_instr, &ret_instr);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &copy_a;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("arithmetic", result, kExpected);
}

/*
Purpose: Verify fused comparison and conditional jump handling.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 1.
Invariants/Assumptions: TACCOND_JUMP carries both comparison operands.

Readable TAC:
  func main:
    if 5 LT 10 goto L_then
    return 2
  L_then:
    return 1
*/
static bool tac_test_cond_jump(void) {
  const int kLeft = 5;
  const int kRight = 10;
  const int kFalseValue = 2;
  const int kTrueValue = 1;
  const int kExpected = 1;
  struct Slice main_name = tac_slice_literal("main");
  struct Slice then_label = tac_slice_literal("L_then");
  struct Val const_5 = tac_val_const(kLeft, &kTestIntType);
  struct Val const_10 = tac_val_const(kRight, &kTestIntType);
  struct Val const_1 = tac_val_const(kTrueValue, &kTestIntType);
  struct Val const_2 = tac_val_const(kFalseValue, &kTestIntType);

  struct TACInstr cond_jump;
  struct TACInstr ret_false;
  struct TACInstr label;
  struct TACInstr ret_true;
  tac_init_instr(&cond_jump, TACCOND_JUMP);
  tac_init_instr(&ret_false, TACRETURN);
  tac_init_instr(&label, TACLABEL);
  tac_init_instr(&ret_true, TACRETURN);

  cond_jump.instr.tac_cond_jump.src1 = &const_5;
  cond_jump.instr.tac_cond_jump.src2 = &const_10;
  cond_jump.instr.tac_cond_jump.condition = CondL;
  cond_jump.instr.tac_cond_jump.label = &then_label;
  ret_false.instr.tac_return.dst = &const_2;
  label.instr.tac_label.label = &then_label;
  ret_true.instr.tac_return.dst = &const_1;

  tac_link_instr(&cond_jump, &ret_false);
  tac_link_instr(&ret_false, &label);
  tac_link_instr(&label, &ret_true);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &cond_jump;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("cond_jump", result, kExpected);
}

/*
Purpose: Verify function calls and parameter passing.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 13.
Invariants/Assumptions: Exercises TACCALL with two parameters.

Readable TAC:
  func add(p, q):
    t0 = p + q
    return t0
  func main:
    t1 = call add(6, 7)
    return t1
*/
static bool tac_test_call(void) {
  const int kArg0 = 6;
  const int kArg1 = 7;
  const int kExpected = 13;
  enum { kArgCount = 2 };
  struct Slice add_name = tac_slice_literal("add");
  struct Slice p_name = tac_slice_literal("p");
  struct Slice q_name = tac_slice_literal("q");
  struct Slice t0_name = tac_slice_literal("t0");
  struct Slice main_name = tac_slice_literal("main");
  struct Slice t1_name = tac_slice_literal("t1");

  struct Val p_val = tac_val_var(&p_name, &kTestIntType);
  struct Val q_val = tac_val_var(&q_name, &kTestIntType);
  struct Val t0_val = tac_val_var(&t0_name, &kTestIntType);
  struct Val t1_val = tac_val_var(&t1_name, &kTestIntType);

  struct TACInstr add_bin;
  struct TACInstr add_ret;
  tac_init_instr(&add_bin, TACBINARY);
  tac_init_instr(&add_ret, TACRETURN);
  add_bin.instr.tac_binary.alu_op = ALU_ADD;
  add_bin.instr.tac_binary.dst = &t0_val;
  add_bin.instr.tac_binary.src1 = &p_val;
  add_bin.instr.tac_binary.src2 = &q_val;
  add_ret.instr.tac_return.dst = &t0_val;
  tac_link_instr(&add_bin, &add_ret);

  struct Slice* add_params[kArgCount] = { &p_name, &q_name };
  struct TopLevel add_func = {0};
  add_func.type = FUNC;
  add_func.name = &add_name;
  add_func.global = true;
  add_func.body = &add_bin;
  add_func.params = add_params;
  add_func.num_params = kArgCount;
  add_func.next = NULL;

  struct Val call_args[kArgCount];
  call_args[0] = tac_val_const(kArg0, &kTestIntType);
  call_args[1] = tac_val_const(kArg1, &kTestIntType);

  struct TACInstr call_instr;
  struct TACInstr main_ret;
  tac_init_instr(&call_instr, TACCALL);
  tac_init_instr(&main_ret, TACRETURN);
  call_instr.instr.tac_call.func_name = &add_name;
  call_instr.instr.tac_call.dst = &t1_val;
  call_instr.instr.tac_call.args = call_args;
  call_instr.instr.tac_call.num_args = kArgCount;
  main_ret.instr.tac_return.dst = &t1_val;
  tac_link_instr(&call_instr, &main_ret);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &call_instr;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  add_func.next = &main_func;

  struct TACProg prog = {0};
  prog.head = &add_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("call", result, kExpected);
}

/*
Purpose: Verify address-of, load, and store operations.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 42.
Invariants/Assumptions: Exercises TACGET_ADDRESS, TACSTORE, and TACLOAD.

Readable TAC:
  func main:
    x = 10
    p = &x
    store [p], 42
    t0 = load [p]
    return t0
*/
static bool tac_test_memory_ops(void) {
  const int kInitialValue = 10;
  const int kStoredValue = 42;
  const int kExpected = 42;
  struct Slice main_name = tac_slice_literal("main");
  struct Slice x_name = tac_slice_literal("x");
  struct Slice p_name = tac_slice_literal("p");
  struct Slice t0_name = tac_slice_literal("t0");

  struct Val x_val = tac_val_var(&x_name, &kTestIntType);
  struct Val p_val = tac_val_var(&p_name, &kTestPtrType);
  struct Val t0_val = tac_val_var(&t0_name, &kTestIntType);
  struct Val const_10 = tac_val_const(kInitialValue, &kTestIntType);
  struct Val const_42 = tac_val_const(kStoredValue, &kTestIntType);

  struct TACInstr copy_x;
  struct TACInstr addr_of;
  struct TACInstr store;
  struct TACInstr load;
  struct TACInstr ret_instr;
  tac_init_instr(&copy_x, TACCOPY);
  tac_init_instr(&addr_of, TACGET_ADDRESS);
  tac_init_instr(&store, TACSTORE);
  tac_init_instr(&load, TACLOAD);
  tac_init_instr(&ret_instr, TACRETURN);

  copy_x.instr.tac_copy.dst = &x_val;
  copy_x.instr.tac_copy.src = &const_10;
  addr_of.instr.tac_get_address.dst = &p_val;
  addr_of.instr.tac_get_address.src = &x_val;
  store.instr.tac_store.dst_ptr = &p_val;
  store.instr.tac_store.src = &const_42;
  load.instr.tac_load.dst = &t0_val;
  load.instr.tac_load.src_ptr = &p_val;
  ret_instr.instr.tac_return.dst = &t0_val;

  tac_link_instr(&copy_x, &addr_of);
  tac_link_instr(&addr_of, &store);
  tac_link_instr(&store, &load);
  tac_link_instr(&load, &ret_instr);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &copy_x;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("memory_ops", result, kExpected);
}

/*
Purpose: Verify unary operators and result aggregation.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter matches C unary semantics.
Invariants/Assumptions: Exercises TACUNARY with NEGATE, COMPLEMENT, and BOOL_NOT.

Readable TAC:
  func main:
    a = 5
    neg = -a
    comp = ~a
    bnot = !a
    t0 = neg + comp
    t1 = t0 + bnot
    return t1
*/
static bool tac_test_unary_ops(void) {
  const int kInput = 5; // Non-zero input ensures BOOL_NOT yields 0.
  const int kExpected = (-kInput) + (~kInput) + (!kInput);
  struct Slice main_name = tac_slice_literal("main");
  struct Slice a_name = tac_slice_literal("a");
  struct Slice neg_name = tac_slice_literal("neg");
  struct Slice comp_name = tac_slice_literal("comp");
  struct Slice bnot_name = tac_slice_literal("bnot");
  struct Slice t0_name = tac_slice_literal("t0");
  struct Slice t1_name = tac_slice_literal("t1");

  struct Val a_val = tac_val_var(&a_name, &kTestIntType);
  struct Val neg_val = tac_val_var(&neg_name, &kTestIntType);
  struct Val comp_val = tac_val_var(&comp_name, &kTestIntType);
  struct Val bnot_val = tac_val_var(&bnot_name, &kTestIntType);
  struct Val t0_val = tac_val_var(&t0_name, &kTestIntType);
  struct Val t1_val = tac_val_var(&t1_name, &kTestIntType);
  struct Val const_input = tac_val_const(kInput, &kTestIntType);

  struct TACInstr copy_a;
  struct TACInstr unary_neg;
  struct TACInstr unary_comp;
  struct TACInstr unary_not;
  struct TACInstr add_0;
  struct TACInstr add_1;
  struct TACInstr ret_instr;
  tac_init_instr(&copy_a, TACCOPY);
  tac_init_instr(&unary_neg, TACUNARY);
  tac_init_instr(&unary_comp, TACUNARY);
  tac_init_instr(&unary_not, TACUNARY);
  tac_init_instr(&add_0, TACBINARY);
  tac_init_instr(&add_1, TACBINARY);
  tac_init_instr(&ret_instr, TACRETURN);

  copy_a.instr.tac_copy.dst = &a_val;
  copy_a.instr.tac_copy.src = &const_input;

  unary_neg.instr.tac_unary.op = NEGATE;
  unary_neg.instr.tac_unary.dst = &neg_val;
  unary_neg.instr.tac_unary.src = &a_val;

  unary_comp.instr.tac_unary.op = COMPLEMENT;
  unary_comp.instr.tac_unary.dst = &comp_val;
  unary_comp.instr.tac_unary.src = &a_val;

  unary_not.instr.tac_unary.op = BOOL_NOT;
  unary_not.instr.tac_unary.dst = &bnot_val;
  unary_not.instr.tac_unary.src = &a_val;

  add_0.instr.tac_binary.alu_op = ALU_ADD;
  add_0.instr.tac_binary.dst = &t0_val;
  add_0.instr.tac_binary.src1 = &neg_val;
  add_0.instr.tac_binary.src2 = &comp_val;

  add_1.instr.tac_binary.alu_op = ALU_ADD;
  add_1.instr.tac_binary.dst = &t1_val;
  add_1.instr.tac_binary.src1 = &t0_val;
  add_1.instr.tac_binary.src2 = &bnot_val;

  ret_instr.instr.tac_return.dst = &t1_val;

  tac_link_instr(&copy_a, &unary_neg);
  tac_link_instr(&unary_neg, &unary_comp);
  tac_link_instr(&unary_comp, &unary_not);
  tac_link_instr(&unary_not, &add_0);
  tac_link_instr(&add_0, &add_1);
  tac_link_instr(&add_1, &ret_instr);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &copy_a;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("unary_ops", result, kExpected);
}

/*
Purpose: Verify unconditional jumps and label dispatch.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 9.
Invariants/Assumptions: Exercises TACJUMP and TACLABEL.

Readable TAC:
  func main:
    jmp L1
    return 0
  L1:
    return 9
*/
static bool tac_test_jump(void) {
  const int kSkipped = 0;
  const int kReturned = 9;
  const int kExpected = 9;
  struct Slice main_name = tac_slice_literal("main");
  struct Slice label_name = tac_slice_literal("L1");

  struct Val const_0 = tac_val_const(kSkipped, &kTestIntType);
  struct Val const_9 = tac_val_const(kReturned, &kTestIntType);

  struct TACInstr jump_instr;
  struct TACInstr ret_false;
  struct TACInstr label_instr;
  struct TACInstr ret_true;
  tac_init_instr(&jump_instr, TACJUMP);
  tac_init_instr(&ret_false, TACRETURN);
  tac_init_instr(&label_instr, TACLABEL);
  tac_init_instr(&ret_true, TACRETURN);

  jump_instr.instr.tac_jump.label = &label_name;
  ret_false.instr.tac_return.dst = &const_0;
  label_instr.instr.tac_label.label = &label_name;
  ret_true.instr.tac_return.dst = &const_9;

  tac_link_instr(&jump_instr, &ret_false);
  tac_link_instr(&ret_false, &label_instr);
  tac_link_instr(&label_instr, &ret_true);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &jump_instr;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("jump", result, kExpected);
}

/*
Purpose: Verify CopyToOffset and address arithmetic for loads.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 99.
Invariants/Assumptions: Exercises TACCOPY_TO_OFFSET, TACGET_ADDRESS, and TACLOAD.

Readable TAC:
  func main:
    arr0 = 0
    p = &arr0
    copy_to_offset arr0, 99, 1
    addr2 = p + 1
    t0 = load [addr2]
    return t0
*/
static bool tac_test_copy_to_offset(void) {
  const int kInitialValue = 0;
  const int kStoredValue = 99;
  const int kOffsetBytes = 1; // Byte offset; validates byte-addressed semantics.
  const int kExpected = 99;
  struct Slice main_name = tac_slice_literal("main");
  struct Slice arr0_name = tac_slice_literal("arr0");
  struct Slice p_name = tac_slice_literal("p");
  struct Slice addr2_name = tac_slice_literal("addr2");
  struct Slice t0_name = tac_slice_literal("t0");

  struct Val arr0_val = tac_val_var(&arr0_name, &kTestIntType);
  struct Val p_val = tac_val_var(&p_name, &kTestPtrType);
  struct Val addr2_val = tac_val_var(&addr2_name, &kTestPtrType);
  struct Val t0_val = tac_val_var(&t0_name, &kTestIntType);
  struct Val const_init = tac_val_const(kInitialValue, &kTestIntType);
  struct Val const_store = tac_val_const(kStoredValue, &kTestIntType);
  struct Val const_offset = tac_val_const(kOffsetBytes, &kTestIntType);

  struct TACInstr copy_arr0;
  struct TACInstr addr_of;
  struct TACInstr copy_offset;
  struct TACInstr add_addr;
  struct TACInstr load;
  struct TACInstr ret_instr;
  tac_init_instr(&copy_arr0, TACCOPY);
  tac_init_instr(&addr_of, TACGET_ADDRESS);
  tac_init_instr(&copy_offset, TACCOPY_TO_OFFSET);
  tac_init_instr(&add_addr, TACBINARY);
  tac_init_instr(&load, TACLOAD);
  tac_init_instr(&ret_instr, TACRETURN);

  copy_arr0.instr.tac_copy.dst = &arr0_val;
  copy_arr0.instr.tac_copy.src = &const_init;
  addr_of.instr.tac_get_address.dst = &p_val;
  addr_of.instr.tac_get_address.src = &arr0_val;
  copy_offset.instr.tac_copy_to_offset.dst = &arr0_name;
  copy_offset.instr.tac_copy_to_offset.src = &const_store;
  copy_offset.instr.tac_copy_to_offset.offset = kOffsetBytes;
  add_addr.instr.tac_binary.alu_op = ALU_ADD;
  add_addr.instr.tac_binary.dst = &addr2_val;
  add_addr.instr.tac_binary.src1 = &p_val;
  add_addr.instr.tac_binary.src2 = &const_offset;
  load.instr.tac_load.dst = &t0_val;
  load.instr.tac_load.src_ptr = &addr2_val;
  ret_instr.instr.tac_return.dst = &t0_val;

  tac_link_instr(&copy_arr0, &addr_of);
  tac_link_instr(&addr_of, &copy_offset);
  tac_link_instr(&copy_offset, &add_addr);
  tac_link_instr(&add_addr, &load);
  tac_link_instr(&load, &ret_instr);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &copy_arr0;
  main_func.params = NULL;
  main_func.num_params = 0;
  main_func.next = NULL;

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("copy_to_offset", result, kExpected);
}

// Purpose: Check a constant-folding replacement and its typed result.
// Inputs: name identifies the case; instr is the original instruction;
// expected_dst/type/value describe the required TACCOPY.
// Outputs: Returns true when all replacement fields match.
// Invariants/Assumptions: The compiler arena is initialized.
static bool tac_expect_folded_constant(const char* name,
                                       struct TACInstr* instr,
                                       struct Val* expected_dst,
                                       struct Type* expected_type,
                                       uint64_t expected_value) {
  struct TACInstr* folded = constant_fold(instr);
  if (folded != NULL && folded != instr && folded->type == TACCOPY &&
      folded->instr.tac_copy.dst == expected_dst &&
      folded->instr.tac_copy.src != NULL &&
      folded->instr.tac_copy.src->val_type == CONSTANT &&
      folded->instr.tac_copy.src->type == expected_type &&
      folded->instr.tac_copy.src->val.const_value == expected_value) {
    return true;
  }

  printf("constant-folding test %s failed: expected a typed constant copy "
         "with value 0x%016llx\n",
         name, (unsigned long long)expected_value);
  return false;
}

/*
Purpose: Verify constant folding for operand selection, integer signedness,
truncation, sign extension, and fused conditional jumps.
Inputs: None (builds isolated TAC instructions in-place).
Outputs: Returns true when each instruction has the expected folded form.
Invariants/Assumptions: Constants use the TAC raw-bit representation.
*/
static bool tac_test_constant_folding(void) {
  struct Type long_type = { .type = LONG_TYPE };
  struct Type uint_type = { .type = UINT_TYPE };
  struct Type uchar_type = { .type = UCHAR_TYPE };
  struct Type schar_type = { .type = SCHAR_TYPE };
  struct Slice dst_name = tac_slice_literal("fold.dst");
  struct Slice right_name = tac_slice_literal("fold.right");
  struct Slice target_name = tac_slice_literal("fold.target");
  struct Val int_dst = tac_val_var(&dst_name, &kTestIntType);
  struct Val long_dst = tac_val_var(&dst_name, &long_type);
  struct Val uchar_dst = tac_val_var(&dst_name, &uchar_type);
  struct Val variable_right = tac_val_var(&right_name, &kTestIntType);
  struct Val one = tac_val_const(1, &kTestIntType);
  struct Val two = tac_val_const(2, &kTestIntType);
  struct Val minus_seven_bits =
      tac_val_const_bits(UINT64_C(0xfffffff9), &kTestIntType);
  struct Val minus_twenty_thousand_bits =
      tac_val_const_bits(UINT64_C(0xffffb1e0), &kTestIntType);
  struct Val int_min_bits =
      tac_val_const_bits(UINT64_C(0x80000000), &kTestIntType);
  struct Val minus_one_bits =
      tac_val_const_bits(UINT64_C(0xffffffff), &kTestIntType);
  struct Val uint_max = tac_val_const_bits(UINT64_C(0xffffffff), &uint_type);
  struct Val uint_zero = tac_val_const_bits(UINT64_C(0), &uint_type);
  struct Val all_bits = tac_val_const_bits(UINT64_MAX, &long_type);
  struct Val signed_byte_min = tac_val_const_bits(UINT64_C(0x80), &schar_type);
  struct TACInstr instr;
  bool ok = true;

  arena_init(1024);

  tac_init_instr(&instr, TACBINARY);
  instr.instr.tac_binary.alu_op = ALU_MOV;
  instr.instr.tac_binary.dst = &int_dst;
  instr.instr.tac_binary.src1 = &one;
  instr.instr.tac_binary.src2 = &variable_right;
  struct TACInstr* folded_move = constant_fold(&instr);
  if (folded_move == NULL || folded_move == &instr ||
      folded_move->type != TACCOPY ||
      folded_move->last != folded_move ||
      folded_move->instr.tac_copy.dst != &int_dst ||
      folded_move->instr.tac_copy.src != &variable_right) {
    printf("constant-folding test ALU_MOV failed: expected the second operand "
           "to be copied\n");
    ok = false;
  }

  tac_init_instr(&instr, TACBINARY);
  instr.instr.tac_binary.alu_op = ALU_SDIV;
  instr.instr.tac_binary.dst = &int_dst;
  instr.instr.tac_binary.src1 = &minus_seven_bits;
  instr.instr.tac_binary.src2 = &two;
  ok = tac_expect_folded_constant("signed division", &instr, &int_dst,
                                  &kTestIntType, UINT64_MAX - UINT64_C(2)) && ok;

  tac_init_instr(&instr, TACBINARY);
  instr.instr.tac_binary.alu_op = ALU_ASR;
  instr.instr.tac_binary.dst = &int_dst;
  instr.instr.tac_binary.src1 = &minus_twenty_thousand_bits;
  instr.instr.tac_binary.src2 = &two;
  ok = tac_expect_folded_constant("arithmetic right shift", &instr, &int_dst,
                                  &kTestIntType,
                                  (uint64_t)(int64_t)-5000) && ok;

  tac_init_instr(&instr, TACTRUNC);
  instr.instr.tac_trunc.dst = &int_dst;
  instr.instr.tac_trunc.src = &all_bits;
  instr.instr.tac_trunc.target_size = sizeof(uint32_t);
  ok = tac_expect_folded_constant("signed truncation", &instr, &int_dst,
                                  &kTestIntType, UINT64_MAX) && ok;

  tac_init_instr(&instr, TACTRUNC);
  instr.instr.tac_trunc.dst = &uchar_dst;
  instr.instr.tac_trunc.src = &all_bits;
  instr.instr.tac_trunc.target_size = sizeof(uint8_t);
  ok = tac_expect_folded_constant("unsigned truncation", &instr, &uchar_dst,
                                  &uchar_type, UINT64_C(0xff)) && ok;

  tac_init_instr(&instr, TACEXTEND);
  instr.instr.tac_extend.dst = &long_dst;
  instr.instr.tac_extend.src = &signed_byte_min;
  instr.instr.tac_extend.src_size = sizeof(uint8_t);
  ok = tac_expect_folded_constant("sign extension", &instr, &long_dst,
                                  &long_type,
                                  (uint64_t)(int64_t)-128) && ok;

  tac_init_instr(&instr, TACBINARY);
  instr.instr.tac_binary.alu_op = ALU_SDIV;
  instr.instr.tac_binary.dst = &int_dst;
  instr.instr.tac_binary.src1 = &one;
  struct Val zero = tac_val_const(0, &kTestIntType);
  instr.instr.tac_binary.src2 = &zero;
  if (constant_fold(&instr) != &instr) {
    printf("constant-folding test division by zero failed: undefined "
           "operation must remain unfolded\n");
    ok = false;
  }

  tac_init_instr(&instr, TACBINARY);
  instr.instr.tac_binary.alu_op = ALU_SDIV;
  instr.instr.tac_binary.dst = &int_dst;
  instr.instr.tac_binary.src1 = &int_min_bits;
  instr.instr.tac_binary.src2 = &minus_one_bits;
  if (constant_fold(&instr) != &instr) {
    printf("constant-folding test signed division overflow failed: undefined "
           "operation must remain unfolded\n");
    ok = false;
  }

  tac_init_instr(&instr, TACCOND_JUMP);
  instr.instr.tac_cond_jump.src1 = &minus_one_bits;
  instr.instr.tac_cond_jump.src2 = &zero;
  instr.instr.tac_cond_jump.condition = CondL;
  instr.instr.tac_cond_jump.label = &target_name;
  struct TACInstr* folded_jump = constant_fold(&instr);
  if (folded_jump == NULL || folded_jump == &instr ||
      folded_jump->type != TACJUMP ||
      folded_jump->instr.tac_jump.label != &target_name) {
    printf("constant-folding test signed conditional jump failed: expected "
           "an unconditional jump\n");
    ok = false;
  }

  struct TACInstr return_after_jump;
  tac_init_instr(&instr, TACCOND_JUMP);
  tac_init_instr(&return_after_jump, TACRETURN);
  instr.instr.tac_cond_jump.src1 = &uint_max;
  instr.instr.tac_cond_jump.src2 = &uint_zero;
  instr.instr.tac_cond_jump.condition = CondB;
  instr.instr.tac_cond_jump.label = &target_name;
  return_after_jump.instr.tac_return.dst = &one;
  tac_link_instr(&instr, &return_after_jump);
  struct TACInstr* folded_fallthrough = constant_fold(&instr);
  if (folded_fallthrough != &return_after_jump ||
      folded_fallthrough->last != &return_after_jump) {
    printf("constant-folding test unsigned conditional jump failed: expected "
           "the never-taken jump to be removed\n");
    ok = false;
  }

  arena_destroy();
  return ok;
}

// Purpose: Check one expected result from TAC body comparison.
// Inputs: name identifies the field under test; left/right are TAC bodies.
// Outputs: Returns true when compare_bodies produces expected.
// Invariants/Assumptions: The bodies are acyclic.
static bool tac_expect_body_comparison(const char* name, struct TACInstr* left,
                                       struct TACInstr* right, bool expected) {
  bool actual = compare_bodies(left, right);
  if (actual == expected) {
    return true;
  }
  printf("TAC comparison test %s failed: expected %s, got %s\n",
         name, expected ? "equal" : "different", actual ? "equal" : "different");
  return false;
}

/*
Purpose: Verify equality for every TAC instruction payload.
Inputs: None (builds isolated instruction nodes in-place).
Outputs: Returns true when identical payloads compare equal and every changed
field compares different.
Invariants/Assumptions: TAC references intentionally use pointer identity.
*/
static bool tac_test_compare_bodies(void) {
  const size_t kFirstCount = 1;
  const size_t kSecondCount = 2;
  const int kFirstOffset = 4;
  const int kSecondOffset = 8;
  const size_t kFirstSize = 2;
  const size_t kSecondSize = 4;
  struct Slice first_name = tac_slice_literal("first");
  struct Slice second_name = tac_slice_literal("second");
  struct Val first_val = tac_val_var(&first_name, &kTestIntType);
  struct Val second_val = tac_val_var(&second_name, &kTestIntType);
  struct Val first_args[1] = { first_val };
  struct Val second_args[1] = { second_val };
  char first_loc;
  char second_loc;
  struct TACInstr left;
  struct TACInstr right;
  struct TACInstr extra;
  bool ok = true;

#define EXPECT_MATCH() do {                                                        \
    right = left;                                                                 \
    ok = tac_expect_body_comparison("identical payload", &left, &right, true)     \
         && ok;                                                                   \
  } while (false)
#define EXPECT_FIELD_DIFFERENT(FIELD, VALUE) do {                                 \
    right = left;                                                                 \
    right.FIELD = (VALUE);                                                        \
    ok = tac_expect_body_comparison(#FIELD, &left, &right, false) && ok;          \
  } while (false)

  ok = tac_expect_body_comparison("empty bodies", NULL, NULL, true) && ok;

  tac_init_instr(&left, TACRETURN);
  left.instr.tac_return.dst = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_return.dst, &second_val);

  tac_init_instr(&left, TACUNARY);
  left.instr.tac_unary.op = NEGATE;
  left.instr.tac_unary.dst = &first_val;
  left.instr.tac_unary.src = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_unary.op, BOOL_NOT);
  EXPECT_FIELD_DIFFERENT(instr.tac_unary.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_unary.src, &second_val);

  tac_init_instr(&left, TACBINARY);
  left.instr.tac_binary.alu_op = ALU_ADD;
  left.instr.tac_binary.dst = &first_val;
  left.instr.tac_binary.src1 = &first_val;
  left.instr.tac_binary.src2 = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_binary.alu_op, ALU_SUB);
  EXPECT_FIELD_DIFFERENT(instr.tac_binary.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_binary.src1, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_binary.src2, &second_val);

  tac_init_instr(&left, TACCOND_JUMP);
  left.instr.tac_cond_jump.src1 = &first_val;
  left.instr.tac_cond_jump.src2 = &first_val;
  left.instr.tac_cond_jump.condition = CondE;
  left.instr.tac_cond_jump.label = &first_name;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_cond_jump.src1, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_cond_jump.src2, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_cond_jump.condition, CondNE);
  EXPECT_FIELD_DIFFERENT(instr.tac_cond_jump.label, &second_name);

  tac_init_instr(&left, TACJUMP);
  left.instr.tac_jump.label = &first_name;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_jump.label, &second_name);

  tac_init_instr(&left, TACLABEL);
  left.instr.tac_label.label = &first_name;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_label.label, &second_name);

  tac_init_instr(&left, TACCOPY);
  left.instr.tac_copy.dst = &first_val;
  left.instr.tac_copy.src = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_copy.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy.src, &second_val);

  tac_init_instr(&left, TACCALL);
  left.instr.tac_call.func_name = &first_name;
  left.instr.tac_call.dst = &first_val;
  left.instr.tac_call.args = first_args;
  left.instr.tac_call.num_args = kFirstCount;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_call.func_name, &second_name);
  EXPECT_FIELD_DIFFERENT(instr.tac_call.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_call.args, second_args);
  EXPECT_FIELD_DIFFERENT(instr.tac_call.num_args, kSecondCount);

  tac_init_instr(&left, TACCALL_INDIRECT);
  left.instr.tac_call_indirect.func = &first_val;
  left.instr.tac_call_indirect.dst = &first_val;
  left.instr.tac_call_indirect.args = first_args;
  left.instr.tac_call_indirect.num_args = kFirstCount;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_call_indirect.func, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_call_indirect.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_call_indirect.args, second_args);
  EXPECT_FIELD_DIFFERENT(instr.tac_call_indirect.num_args, kSecondCount);

  tac_init_instr(&left, TACGET_ADDRESS);
  left.instr.tac_get_address.dst = &first_val;
  left.instr.tac_get_address.src = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_get_address.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_get_address.src, &second_val);

  tac_init_instr(&left, TACLOAD);
  left.instr.tac_load.dst = &first_val;
  left.instr.tac_load.src_ptr = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_load.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_load.src_ptr, &second_val);

  tac_init_instr(&left, TACSTORE);
  left.instr.tac_store.dst_ptr = &first_val;
  left.instr.tac_store.src = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_store.dst_ptr, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_store.src, &second_val);

  tac_init_instr(&left, TACCOPY_TO_OFFSET);
  left.instr.tac_copy_to_offset.dst = &first_name;
  left.instr.tac_copy_to_offset.src = &first_val;
  left.instr.tac_copy_to_offset.offset = kFirstOffset;
  left.instr.tac_copy_to_offset.dst_type = &kTestIntType;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_to_offset.dst, &second_name);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_to_offset.src, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_to_offset.offset, kSecondOffset);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_to_offset.dst_type, &kTestPtrType);

  tac_init_instr(&left, TACCOPY_FROM_OFFSET);
  left.instr.tac_copy_from_offset.dst = &first_val;
  left.instr.tac_copy_from_offset.src = &first_name;
  left.instr.tac_copy_from_offset.offset = kFirstOffset;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_from_offset.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_from_offset.src, &second_name);
  EXPECT_FIELD_DIFFERENT(instr.tac_copy_from_offset.offset, kSecondOffset);

  tac_init_instr(&left, TACBOUNDARY);
  left.instr.tac_boundary.loc = &first_loc;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_boundary.loc, &second_loc);

  tac_init_instr(&left, TACTRUNC);
  left.instr.tac_trunc.dst = &first_val;
  left.instr.tac_trunc.src = &first_val;
  left.instr.tac_trunc.target_size = kFirstSize;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_trunc.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_trunc.src, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_trunc.target_size, kSecondSize);

  tac_init_instr(&left, TACEXTEND);
  left.instr.tac_extend.dst = &first_val;
  left.instr.tac_extend.src = &first_val;
  left.instr.tac_extend.src_size = kFirstSize;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_extend.dst, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_extend.src, &second_val);
  EXPECT_FIELD_DIFFERENT(instr.tac_extend.src_size, kSecondSize);

  tac_init_instr(&right, TACRETURN);
  ok = tac_expect_body_comparison("instruction type", &left, &right, false) && ok;
  tac_init_instr(&extra, TACRETURN);
  left.next = &extra;
  ok = tac_expect_body_comparison("body length", &left, &right, false) && ok;

#undef EXPECT_FIELD_DIFFERENT
#undef EXPECT_MATCH

  return ok;
}

// Purpose: Run all TAC interpreter tests.
// Inputs: None.
// Outputs: Returns 0 on success and non-zero on failure.
// Invariants/Assumptions: Each test builds its own TAC program.
int main(void) {
  bool ok = true;
  printf("- tac_test_return_const\n");
  ok = tac_test_return_const() && ok;
  printf("- tac_test_arithmetic\n");
  ok = tac_test_arithmetic() && ok;
  printf("- tac_test_cond_jump\n");
  ok = tac_test_cond_jump() && ok;
  printf("- tac_test_call\n");
  ok = tac_test_call() && ok;
  printf("- tac_test_memory_ops\n");
  ok = tac_test_memory_ops() && ok;
  printf("- tac_test_unary_ops\n");
  ok = tac_test_unary_ops() && ok;
  printf("- tac_test_jump\n");
  ok = tac_test_jump() && ok;
  printf("- tac_test_copy_to_offset\n");
  ok = tac_test_copy_to_offset() && ok;
  printf("- tac_test_constant_folding\n");
  ok = tac_test_constant_folding() && ok;
  printf("- tac_test_compare_bodies\n");
  ok = tac_test_compare_bodies() && ok;

  if (ok) {
    printf("TAC interpreter tests passed. ");
    return 0;
  }
  return 1;
}
