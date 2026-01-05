#include "TAC.h"
#include "slice.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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
// Inputs: value is the integer literal to store.
// Outputs: Returns a Val tagged as CONSTANT.
// Invariants/Assumptions: Uses int for all constants.
static struct Val tac_val_const(int value) {
  struct Val val;
  val.val_type = CONSTANT;
  val.val.const_value = value;
  return val;
}

// Purpose: Build a variable TAC value for test data.
// Inputs: name is the variable identifier slice.
// Outputs: Returns a Val tagged as VARIABLE.
// Invariants/Assumptions: name must outlive the test execution.
static struct Val tac_val_var(struct Slice* name) {
  struct Val val;
  val.val_type = VARIABLE;
  val.val.var_name = name;
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
  struct Val ret_val = tac_val_const(kReturnValue);

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

  struct Val a_val = tac_val_var(&a_name);
  struct Val b_val = tac_val_var(&b_name);
  struct Val t0_val = tac_val_var(&t0_name);
  struct Val t1_val = tac_val_var(&t1_name);
  struct Val const_3 = tac_val_const(kFirst);
  struct Val const_4 = tac_val_const(kSecond);
  struct Val const_2 = tac_val_const(kMultiplier);

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

  add_instr.instr.tac_binary.op = ADD_OP;
  add_instr.instr.tac_binary.dst = &t0_val;
  add_instr.instr.tac_binary.src1 = &a_val;
  add_instr.instr.tac_binary.src2 = &b_val;
  add_instr.instr.tac_binary.type = NULL;

  mul_instr.instr.tac_binary.op = MUL_OP;
  mul_instr.instr.tac_binary.dst = &t1_val;
  mul_instr.instr.tac_binary.src1 = &t0_val;
  mul_instr.instr.tac_binary.src2 = &const_2;
  mul_instr.instr.tac_binary.type = NULL;

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
Purpose: Verify compare and conditional jump handling.
Inputs: None (builds a TACProg in-place).
Outputs: Returns true when the interpreter returns 1.
Invariants/Assumptions: Exercises TACCMP and TACCOND_JUMP.

Readable TAC:
  func main:
    cmp 5, 10
    if LT goto L_then
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
  struct Val const_5 = tac_val_const(kLeft);
  struct Val const_10 = tac_val_const(kRight);
  struct Val const_1 = tac_val_const(kTrueValue);
  struct Val const_2 = tac_val_const(kFalseValue);

  struct TACInstr cmp_instr;
  struct TACInstr cond_jump;
  struct TACInstr ret_false;
  struct TACInstr label;
  struct TACInstr ret_true;
  tac_init_instr(&cmp_instr, TACCMP);
  tac_init_instr(&cond_jump, TACCOND_JUMP);
  tac_init_instr(&ret_false, TACRETURN);
  tac_init_instr(&label, TACLABEL);
  tac_init_instr(&ret_true, TACRETURN);

  cmp_instr.instr.tac_cmp.src1 = &const_5;
  cmp_instr.instr.tac_cmp.src2 = &const_10;
  cond_jump.instr.tac_cond_jump.condition = CondL;
  cond_jump.instr.tac_cond_jump.label = &then_label;
  ret_false.instr.tac_return.dst = &const_2;
  label.instr.tac_label.label = &then_label;
  ret_true.instr.tac_return.dst = &const_1;

  tac_link_instr(&cmp_instr, &cond_jump);
  tac_link_instr(&cond_jump, &ret_false);
  tac_link_instr(&ret_false, &label);
  tac_link_instr(&label, &ret_true);

  struct TopLevel main_func = {0};
  main_func.type = FUNC;
  main_func.name = &main_name;
  main_func.global = true;
  main_func.body = &cmp_instr;
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

  struct Val p_val = tac_val_var(&p_name);
  struct Val q_val = tac_val_var(&q_name);
  struct Val t0_val = tac_val_var(&t0_name);
  struct Val t1_val = tac_val_var(&t1_name);

  struct TACInstr add_bin;
  struct TACInstr add_ret;
  tac_init_instr(&add_bin, TACBINARY);
  tac_init_instr(&add_ret, TACRETURN);
  add_bin.instr.tac_binary.op = ADD_OP;
  add_bin.instr.tac_binary.dst = &t0_val;
  add_bin.instr.tac_binary.src1 = &p_val;
  add_bin.instr.tac_binary.src2 = &q_val;
  add_bin.instr.tac_binary.type = NULL;
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
  call_args[0] = tac_val_const(kArg0);
  call_args[1] = tac_val_const(kArg1);

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

  struct Val x_val = tac_val_var(&x_name);
  struct Val p_val = tac_val_var(&p_name);
  struct Val t0_val = tac_val_var(&t0_name);
  struct Val const_10 = tac_val_const(kInitialValue);
  struct Val const_42 = tac_val_const(kStoredValue);

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

  struct Val a_val = tac_val_var(&a_name);
  struct Val neg_val = tac_val_var(&neg_name);
  struct Val comp_val = tac_val_var(&comp_name);
  struct Val bnot_val = tac_val_var(&bnot_name);
  struct Val t0_val = tac_val_var(&t0_name);
  struct Val t1_val = tac_val_var(&t1_name);
  struct Val const_input = tac_val_const(kInput);

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

  add_0.instr.tac_binary.op = ADD_OP;
  add_0.instr.tac_binary.dst = &t0_val;
  add_0.instr.tac_binary.src1 = &neg_val;
  add_0.instr.tac_binary.src2 = &comp_val;
  add_0.instr.tac_binary.type = NULL;

  add_1.instr.tac_binary.op = ADD_OP;
  add_1.instr.tac_binary.dst = &t1_val;
  add_1.instr.tac_binary.src1 = &t0_val;
  add_1.instr.tac_binary.src2 = &bnot_val;
  add_1.instr.tac_binary.type = NULL;

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

  struct Val const_0 = tac_val_const(kSkipped);
  struct Val const_9 = tac_val_const(kReturned);

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
    copy_to_offset p, 99, 1
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

  struct Val arr0_val = tac_val_var(&arr0_name);
  struct Val p_val = tac_val_var(&p_name);
  struct Val addr2_val = tac_val_var(&addr2_name);
  struct Val t0_val = tac_val_var(&t0_name);
  struct Val const_init = tac_val_const(kInitialValue);
  struct Val const_store = tac_val_const(kStoredValue);
  struct Val const_offset = tac_val_const(kOffsetBytes);

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
  copy_offset.instr.tac_copy_to_offset.dst = &p_val;
  copy_offset.instr.tac_copy_to_offset.src = &const_store;
  copy_offset.instr.tac_copy_to_offset.offset = kOffsetBytes;
  add_addr.instr.tac_binary.op = ADD_OP;
  add_addr.instr.tac_binary.dst = &addr2_val;
  add_addr.instr.tac_binary.src1 = &p_val;
  add_addr.instr.tac_binary.src2 = &const_offset;
  add_addr.instr.tac_binary.type = NULL;
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

  if (ok) {
    printf("TAC interpreter tests passed. ");
    return 0;
  }
  return 1;
}
