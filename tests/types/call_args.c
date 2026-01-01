#define TEST_ZERO 0
#define TEST_THREE 3
#define TEST_FOUR 4

long widen(int a, int b) {
  return TEST_ZERO;
}

int main(void) {
  int x = TEST_THREE;
  long y = TEST_FOUR;
  long z = widen(x, y);
  return z > TEST_ZERO;
}
