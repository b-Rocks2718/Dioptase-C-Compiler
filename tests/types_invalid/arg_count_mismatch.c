#define TEST_ZERO 0
#define TEST_ONE 1

int foo(int a, int b) { /* Provide the cleanup callback used by this test. */
  return TEST_ZERO;
}

int main(void) { /* Exercise arg count mismatch behavior. */
  return foo(TEST_ONE);
}
