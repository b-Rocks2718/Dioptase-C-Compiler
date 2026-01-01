#define TEST_ZERO 0
#define TEST_ONE 1

int foo(int a, int b) {
  return TEST_ZERO;
}

int main(void) {
  return foo(TEST_ONE);
}
