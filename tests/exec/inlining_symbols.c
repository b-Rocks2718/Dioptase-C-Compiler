/*
Verify that inlining preserves static objects and function identities while
giving automatic locals fresh symbol-table-backed names.

The indirect call prevents the optimized body from folding away every inlined
local, so emulator execution also exercises ASM stack-slot allocation.
*/
#define TEST_OK 0
#define TEST_FAIL 1

static int shared = 3;

static int increment(int value) {
  return value + 1;
}

static int calculate(int value) {
  int adjusted = value + shared;
  int (*operation)(int) = increment;
  return operation(adjusted) + shared;
}

int main(void) {
  return calculate(4) == 11 ? TEST_OK : TEST_FAIL;
}
