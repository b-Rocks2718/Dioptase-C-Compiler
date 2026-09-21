#include "TAC.h"
#include "arena.h"
#include "cfg.h"
#include "optimization.h"
#include "slice.h"
#include "constant_fold.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Construct TAC programs directly to verify interpreter and optimization behavior.

static struct Type kTestIntType = { .type = INT_TYPE };
static struct Type kTestUintType = { .type = UINT_TYPE };
static struct Type kTestCharType = { .type = CHAR_TYPE };
static struct Type kTestScharType = { .type = SCHAR_TYPE };
static struct Type kTestPtrType = {
  .type = POINTER_TYPE,
  .type_data.pointer_type = { .referenced_type = &kTestIntType },
};
static struct Type kTestUintPtrType = {
  .type = POINTER_TYPE,
  .type_data.pointer_type = { .referenced_type = &kTestUintType },
};

// Build a Slice from a string literal for test data.
// Returns a Slice referencing the literal.
static struct Slice tac_slice_literal(const char* text) {
  struct Slice slice;
  slice.start = text;
  slice.len = strlen(text);
  return slice;
}

// Build a constant TAC value for test data.
// Returns a Val tagged as CONSTANT.
static struct Val tac_val_const(int value, struct Type* type) {
  struct Val val;
  val.val_type = CONSTANT;
  val.val.const_value = (uint64_t)(int64_t)value;
  val.type = type;
  return val;
}

// Build a constant TAC value from an exact 64-bit bit pattern.
// Returns a Val tagged as CONSTANT.
static struct Val tac_val_const_bits(uint64_t value, struct Type* type) {
  struct Val val;
  val.val_type = CONSTANT;
  val.val.const_value = value;
  val.type = type;
  return val;
}

// Build a variable TAC value for test data.
// Returns a Val tagged as VARIABLE.
// name must outlive the test execution.
static struct Val tac_val_var(struct Slice* name, struct Type* type) {
  struct Val val;
  val.val_type = VARIABLE;
  val.val.var_name = name;
  val.type = type;
  return val;
}

// Initialize a TAC instruction node for tests.
// Clears the node and sets its type, empty reaching-copy list, and links.
static void tac_init_instr(struct TACInstr* instr, enum TACInstrType type) {
  memset(instr, 0, sizeof(*instr));
  instr->type = type;
  instr->reaching_copies.head = NULL;
  instr->reaching_copies.last = NULL;
  instr->next = NULL;
}

// Initialize a TAC function top-level node for tests.
// Clears the node and fills the function variant.
static void tac_init_func(struct TopLevel* top,
                          struct Slice* name,
                          struct TACInstr* body,
                          struct Slice** params,
                          size_t num_params) {
  memset(top, 0, sizeof(*top));
  top->type = FUNC;
  top->top.tac_func.name = name;
  top->top.tac_func.global = true;
  top->top.tac_func.body = body;
  top->top.tac_func.params = params;
  top->top.tac_func.num_params = num_params;
  top->next = NULL;
}

// Link two TAC instruction nodes in a test list.
static void tac_link_instr(struct TACInstr* first, struct TACInstr* second) {
  first->next = second;
}

// Compare interpreter output against an expected value.
// Returns true on success and prints a failure message otherwise.
static bool tac_expect_result(const char* name, int got, int expected) {
  if (got == expected) {
    return true;
  }
  printf("TAC interpreter test %s failed: expected %d, got %d\n", name, expected, got);
  return false;
}

/*
Verify that TACRETURN propagates the constant 7 from main.

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
  ret_instr.instr.tac_return.src = &ret_val;

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &ret_instr, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("return_const", result, kReturnValue);
}

/*
Verify that TACCOPY and TACBINARY evaluate an arithmetic chain to 14.

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

  ret_instr.instr.tac_return.src = &t1_val;

  tac_link_instr(&copy_a, &copy_b);
  tac_link_instr(&copy_b, &add_instr);
  tac_link_instr(&add_instr, &mul_instr);
  tac_link_instr(&mul_instr, &ret_instr);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &copy_a, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("arithmetic", result, kExpected);
}

/*
Verify that TACCOND_JUMP compares both operands and selects the taken result.

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
  ret_false.instr.tac_return.src = &const_2;
  label.instr.tac_label.label = &then_label;
  ret_true.instr.tac_return.src = &const_1;

  tac_link_instr(&cond_jump, &ret_false);
  tac_link_instr(&ret_false, &label);
  tac_link_instr(&label, &ret_true);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &cond_jump, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("cond_jump", result, kExpected);
}

/*
Verify that TACCALL passes two parameters and returns their sum from a callee.

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
  add_ret.instr.tac_return.src = &t0_val;
  tac_link_instr(&add_bin, &add_ret);

  struct Slice* add_params[kArgCount] = { &p_name, &q_name };
  struct TopLevel add_func;
  tac_init_func(&add_func, &add_name, &add_bin, add_params, kArgCount);

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
  main_ret.instr.tac_return.src = &t1_val;
  tac_link_instr(&call_instr, &main_ret);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &call_instr, NULL, 0);

  add_func.next = &main_func;

  struct TACProg prog = {0};
  prog.head = &add_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("call", result, kExpected);
}

/*
Verify that address-of, store, and load round-trip the value 42 through memory.

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
  ret_instr.instr.tac_return.src = &t0_val;

  tac_link_instr(&copy_x, &addr_of);
  tac_link_instr(&addr_of, &store);
  tac_link_instr(&store, &load);
  tac_link_instr(&load, &ret_instr);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &copy_x, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("memory_ops", result, kExpected);
}

/*
Verify that TACUNARY implements C negation, complement, and logical-not semantics.

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

  ret_instr.instr.tac_return.src = &t1_val;

  tac_link_instr(&copy_a, &unary_neg);
  tac_link_instr(&unary_neg, &unary_comp);
  tac_link_instr(&unary_comp, &unary_not);
  tac_link_instr(&unary_not, &add_0);
  tac_link_instr(&add_0, &add_1);
  tac_link_instr(&add_1, &ret_instr);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &copy_a, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("unary_ops", result, kExpected);
}

/*
Verify that TACJUMP finds its label and skips the intervening return.

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
  ret_false.instr.tac_return.src = &const_0;
  label_instr.instr.tac_label.label = &label_name;
  ret_true.instr.tac_return.src = &const_9;

  tac_link_instr(&jump_instr, &ret_false);
  tac_link_instr(&ret_false, &label_instr);
  tac_link_instr(&label_instr, &ret_true);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &jump_instr, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("jump", result, kExpected);
}

/*
Verify that CopyToOffset and pointer arithmetic load from the selected byte offset.

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
  ret_instr.instr.tac_return.src = &t0_val;

  tac_link_instr(&copy_arr0, &addr_of);
  tac_link_instr(&addr_of, &copy_offset);
  tac_link_instr(&copy_offset, &add_addr);
  tac_link_instr(&add_addr, &load);
  tac_link_instr(&load, &ret_instr);

  struct TopLevel main_func;
  tac_init_func(&main_func, &main_name, &copy_arr0, NULL, 0);

  struct TACProg prog = {0};
  prog.head = &main_func;
  prog.tail = &main_func;

  int result = tac_interpret_prog(&prog);
  return tac_expect_result("copy_to_offset", result, kExpected);
}

// Check a constant-folding replacement and its typed result.
// expected_dst/type/value describe the required TACCOPY.
// Returns true when all replacement fields match.
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
Verify constant folding for operand selection, integer signedness,
truncation, sign extension, and fused conditional jumps.
Constants use TAC's raw-bit representation when checking the folded forms.
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
  return_after_jump.instr.tac_return.src = &one;
  tac_link_instr(&instr, &return_after_jump);
  struct TACInstr* folded_fallthrough = constant_fold(&instr);
  if (folded_fallthrough != &return_after_jump) {
    printf("constant-folding test unsigned conditional jump failed: expected "
           "the never-taken jump to be removed\n");
    ok = false;
  }

  arena_destroy();
  return ok;
}

// Check one expected result from TAC body comparison.
// Returns true when compare_bodies produces expected.
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
Verify equality for every TAC instruction payload by changing each field in
turn. TAC references intentionally compare by pointer identity.
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
  left.instr.tac_return.src = &first_val;
  EXPECT_MATCH();
  EXPECT_FIELD_DIFFERENT(instr.tac_return.src, &second_val);

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

/*
Verify that TAC-to-CFG-to-TAC round trips are idempotent and that
CFG-to-TAC rebuilding is repeatable and non-destructive.
Two complete round trips must equal the input, keep valid tail links, and leave
each CFG block independently terminated. build_cfg preserves TAC layout order.
*/
static bool tac_test_cfg_rebuild(void) {
  const unsigned kExpectedNodeCount = 5;
  const unsigned kExpectedInstrCount = 5;
  const size_t kArenaBlockSize = 1024;
  struct Slice target_name = tac_slice_literal("target");
  struct Val zero = tac_val_const(0, &kTestIntType);
  struct Val one = tac_val_const(1, &kTestIntType);
  struct Val condition = tac_val_const(1, &kTestIntType);
  struct TACInstr cond_jump;
  struct TACInstr return_false;
  struct TACInstr label;
  struct TACInstr copy;
  struct TACInstr return_true;
  bool ok = true;

  arena_init(kArenaBlockSize);

  tac_init_instr(&copy, TACCOPY);
  copy.instr.tac_copy.dst = &condition;
  copy.instr.tac_copy.src = &one;
  tac_init_instr(&cond_jump, TACCOND_JUMP);
  cond_jump.instr.tac_cond_jump.src1 = &condition;
  cond_jump.instr.tac_cond_jump.src2 = &zero;
  cond_jump.instr.tac_cond_jump.condition = CondNE;
  cond_jump.instr.tac_cond_jump.label = &target_name;
  tac_init_instr(&return_false, TACRETURN);
  return_false.instr.tac_return.src = &zero;
  tac_init_instr(&label, TACLABEL);
  label.instr.tac_label.label = &target_name;
  tac_init_instr(&return_true, TACRETURN);
  return_true.instr.tac_return.src = &one;

  copy.next = &cond_jump;
  cond_jump.next = &return_false;
  return_false.next = &label;
  label.next = &return_true;

  struct CFG* cfg = build_cfg(&copy);
  if (cfg == NULL || cfg->num_nodes != kExpectedNodeCount) {
    printf("CFG rebuild test setup failed: expected %u CFG nodes\n",
           kExpectedNodeCount);
    arena_destroy();
    return false;
  }

  struct TACInstr* first = rebuild_body(cfg);
  struct TACInstr* second = rebuild_body(cfg);
  if (!compare_bodies(&copy, first) || !compare_bodies(first, second)) {
    printf("CFG rebuild test failed: repeated rebuilds changed TAC instruction order\n");
    ok = false;
  }
  if (first == second || first == cfg->nodes[1]->body.head) {
    printf("CFG rebuild test failed: rebuilt TAC must use detached instruction copies\n");
    ok = false;
  }

  struct CFG* second_cfg = build_cfg(first);
  struct TACInstr* second_round_trip = rebuild_body(second_cfg);
  if (!compare_bodies(first, second_round_trip)) {
    printf("CFG rebuild test failed: a second TAC-to-CFG-to-TAC round trip "
           "changed the body\n");
    ok = false;
  }
  if (second_round_trip == first) {
    printf("CFG rebuild test failed: the second round trip reused its input list\n");
    ok = false;
  }

  unsigned instr_count = 0;
  struct TACInstr* rebuilt_tail = NULL;
  for (struct TACInstr* instr = first; instr != NULL; instr = instr->next) {
    rebuilt_tail = instr;
    instr_count++;
  }
  if (instr_count != kExpectedInstrCount || first == NULL ||
      rebuilt_tail == NULL || rebuilt_tail->next != NULL) {
    printf("CFG rebuild test failed: expected %u instructions in a well-formed list\n",
           kExpectedInstrCount);
    ok = false;
  }

  for (unsigned i = 1; i + 1 < cfg->num_nodes; i++) {
    struct CFGNode* block = cfg->nodes[i];
    if (block->body.last == NULL || block->body.last->next != NULL) {
      printf("CFG rebuild test failed: rebuilding mutated basic block %u\n", i);
      ok = false;
    }
  }

  arena_destroy();
  return ok;
}

/*
Classify names by symbol-table storage: static vars are true,
locals, static consts, functions, and missing names are false.
*/
static bool tac_test_is_static_var(void) {
  const size_t kSymbolBuckets = 8;
  struct Slice static_name = tac_slice_literal("static_x");
  struct Slice local_name = tac_slice_literal("local_x");
  struct Slice const_name = tac_slice_literal("const_x");
  struct Slice fun_name = tac_slice_literal("fun_x");
  struct Slice missing_name = tac_slice_literal("missing_x");
  struct IdentAttr static_attrs = {STATIC_ATTR, true, STATIC, {NO_INIT, NULL}, NULL};
  struct IdentAttr local_attrs = {LOCAL_ATTR, true, NONE, {NO_INIT, NULL}, NULL};
  struct IdentAttr const_attrs = {CONST_ATTR, true, STATIC, {NO_INIT, NULL}, NULL};
  struct IdentAttr fun_attrs = {FUN_ATTR, true, NONE, {NO_INIT, NULL}, NULL};
  struct SymbolTable* saved_table = global_symbol_table;
  bool ok = true;

  arena_init(1024);
  global_symbol_table = create_symbol_table(kSymbolBuckets);
  symbol_table_insert(global_symbol_table, &static_name, &kTestIntType, &static_attrs);
  symbol_table_insert(global_symbol_table, &local_name, &kTestIntType, &local_attrs);
  symbol_table_insert(global_symbol_table, &const_name, &kTestIntType, &const_attrs);
  symbol_table_insert(global_symbol_table, &fun_name, &kTestIntType, &fun_attrs);

  if (!is_static_var(&static_name)) {
    printf("is_static_var test failed: STATIC_ATTR variable should be static\n");
    ok = false;
  }
  if (is_static_var(&local_name)) {
    printf("is_static_var test failed: LOCAL_ATTR variable should not be static\n");
    ok = false;
  }
  if (is_static_var(&const_name)) {
    printf("is_static_var test failed: CONST_ATTR symbol should not be a static variable\n");
    ok = false;
  }
  if (is_static_var(&fun_name)) {
    printf("is_static_var test failed: function symbol should not be a static variable\n");
    ok = false;
  }
  if (is_static_var(&missing_name)) {
    printf("is_static_var test failed: missing symbol should not be static\n");
    ok = false;
  }
  if (is_static_var(NULL)) {
    printf("is_static_var test failed: NULL should not be static\n");
    ok = false;
  }

  global_symbol_table = saved_table;
  arena_destroy();
  return ok;
}

/*
Verify SliceList append, equality-based membership, and node copy sharing.
*/
static bool tac_test_slice_list(void) {
  struct Slice first = tac_slice_literal("first");
  struct Slice second = tac_slice_literal("second");
  struct Slice first_again = tac_slice_literal("first");
  struct Slice missing = tac_slice_literal("missing");
  struct SliceList list = {0};
  bool ok = true;

  arena_init(1024);
  slice_list_add(&list, &first);
  slice_list_add(&list, &second);
  slice_list_add(&list, NULL);

  if (list.head == NULL || list.last == NULL || list.head->next != list.last ||
      list.last->next != NULL) {
    printf("slice_list test failed: expected two linked nodes after two adds\n");
    ok = false;
  }
  if (!slice_list_contains(list, &first) || !slice_list_contains(list, &first_again) ||
      !slice_list_contains(list, &second)) {
    printf("slice_list test failed: added slices should be found by equality\n");
    ok = false;
  }
  if (slice_list_contains(list, &missing) || slice_list_contains(list, NULL)) {
    printf("slice_list test failed: missing and NULL slices should not be found\n");
    ok = false;
  }

  struct SliceList copied = copy_slice_list(list);
  if (copied.head == NULL || copied.head == list.head || copied.head->slice != list.head->slice ||
      !slice_list_contains(copied, &second)) {
    printf("slice_list test failed: copy should duplicate nodes and share slice pointers\n");
    ok = false;
  }

  arena_destroy();
  return ok;
}

/*
Copy recording is type-safe for identical types, char/signed char, and
constant 0 (including null pointers). Signedness-only matches such as
int vs unsigned or unsigned vs pointer are not safe.
*/
static bool tac_test_copy_is_type_safe(void) {
  struct Slice x_name = tac_slice_literal("x");
  struct Slice y_name = tac_slice_literal("y");
  struct Val int_x = tac_val_var(&x_name, &kTestIntType);
  struct Val int_y = tac_val_var(&y_name, &kTestIntType);
  struct Val uint_x = tac_val_var(&x_name, &kTestUintType);
  struct Val char_x = tac_val_var(&x_name, &kTestCharType);
  struct Val schar_y = tac_val_var(&y_name, &kTestScharType);
  struct Val int_ptr = tac_val_var(&x_name, &kTestPtrType);
  struct Val uint_ptr = tac_val_var(&y_name, &kTestUintPtrType);
  struct Val zero_int = tac_val_const(0, &kTestIntType);
  struct Val five_int = tac_val_const(5, &kTestIntType);
  bool ok = true;

  if (!copy_is_type_safe(&int_y, &int_x)) {
    printf("copy_is_type_safe test failed: identical int types should be safe\n");
    ok = false;
  }
  if (copy_is_type_safe(&int_y, &uint_x)) {
    printf("copy_is_type_safe test failed: int to unsigned must not be recorded\n");
    ok = false;
  }
  if (!copy_is_type_safe(&char_x, &schar_y)) {
    printf("copy_is_type_safe test failed: char and signed char should be safe\n");
    ok = false;
  }
  if (copy_is_type_safe(&int_ptr, &uint_x)) {
    printf("copy_is_type_safe test failed: pointer to unsigned must not be recorded\n");
    ok = false;
  }
  if (copy_is_type_safe(&int_ptr, &uint_ptr)) {
    printf("copy_is_type_safe test failed: pointers to different types must not be recorded\n");
    ok = false;
  }
  if (!copy_is_type_safe(&zero_int, &int_ptr)) {
    printf("copy_is_type_safe test failed: constant 0 to a pointer should be safe\n");
    ok = false;
  }
  if (copy_is_type_safe(&five_int, &int_ptr)) {
    printf("copy_is_type_safe test failed: nonzero int to a pointer must not be recorded\n");
    ok = false;
  }
  if (copy_is_type_safe(NULL, &int_x) || copy_is_type_safe(&int_y, NULL)) {
    printf("copy_is_type_safe test failed: NULL operands should not be safe\n");
    ok = false;
  }
  return ok;
}

/*
Aliased vars include statics and address-taken locals, with each name once.
A second GetAddress of the same local, and GetAddress of a static already
taken from the symbol table, must not create duplicates. Locals that are
not address-taken stay out of the list.
*/
static bool tac_test_get_aliased_vars(void) {
  const size_t kSymbolBuckets = 8;
  struct Slice static_name = tac_slice_literal("static_x");
  struct Slice local_name = tac_slice_literal("local_x");
  struct Slice other_local = tac_slice_literal("other_local");
  struct IdentAttr static_attrs = {STATIC_ATTR, true, STATIC, {NO_INIT, NULL}, NULL};
  struct IdentAttr local_attrs = {LOCAL_ATTR, true, NONE, {NO_INIT, NULL}, NULL};
  struct Val local_val = tac_val_var(&local_name, &kTestIntType);
  struct Val static_val = tac_val_var(&static_name, &kTestIntType);
  struct Val ptr_val = tac_val_var(&other_local, &kTestPtrType);
  struct TACInstr get_local;
  struct TACInstr get_local_again;
  struct TACInstr get_static;
  struct SymbolTable* saved_table = global_symbol_table;
  bool ok = true;

  arena_init(1024);
  global_symbol_table = create_symbol_table(kSymbolBuckets);
  symbol_table_insert(global_symbol_table, &static_name, &kTestIntType, &static_attrs);
  symbol_table_insert(global_symbol_table, &local_name, &kTestIntType, &local_attrs);
  symbol_table_insert(global_symbol_table, &other_local, &kTestPtrType, &local_attrs);

  tac_init_instr(&get_local, TACGET_ADDRESS);
  get_local.instr.tac_get_address.dst = &ptr_val;
  get_local.instr.tac_get_address.src = &local_val;
  tac_init_instr(&get_local_again, TACGET_ADDRESS);
  get_local_again.instr.tac_get_address.dst = &ptr_val;
  get_local_again.instr.tac_get_address.src = &local_val;
  tac_init_instr(&get_static, TACGET_ADDRESS);
  get_static.instr.tac_get_address.dst = &ptr_val;
  get_static.instr.tac_get_address.src = &static_val;
  tac_link_instr(&get_local, &get_local_again);
  tac_link_instr(&get_local_again, &get_static);

  struct SliceList aliased = get_aliased_vars(&get_local);
  unsigned count = 0;
  for (struct SliceListNode* node = aliased.head; node != NULL; node = node->next) {
    count++;
  }

  if (count != 2) {
    printf("get_aliased_vars test failed: expected 2 unique names, got %u\n", count);
    ok = false;
  }
  if (!slice_list_contains(aliased, &static_name)) {
    printf("get_aliased_vars test failed: static variables must be treated as aliased\n");
    ok = false;
  }
  if (!slice_list_contains(aliased, &local_name)) {
    printf("get_aliased_vars test failed: address-taken locals must be treated as aliased\n");
    ok = false;
  }
  if (slice_list_contains(aliased, &other_local)) {
    printf("get_aliased_vars test failed: locals that are not address-taken should not be aliased\n");
    ok = false;
  }

  global_symbol_table = saved_table;
  arena_destroy();
  return ok;
}

// Run all TAC interpreter tests.
// Returns 0 on success and non-zero on failure.
int main(void) {
  bool ok = true;
  printf("- tac_test_copy_is_type_safe\n");
  ok = tac_test_copy_is_type_safe() && ok;
  printf("- tac_test_get_aliased_vars\n");
  ok = tac_test_get_aliased_vars() && ok;
  printf("- tac_test_slice_list\n");
  ok = tac_test_slice_list() && ok;
  printf("- tac_test_is_static_var\n");
  ok = tac_test_is_static_var() && ok;
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
  printf("- tac_test_cfg_rebuild\n");
  ok = tac_test_cfg_rebuild() && ok;

  if (ok) {
    printf("TAC interpreter tests passed. ");
    return 0;
  }
  return 1;
}
