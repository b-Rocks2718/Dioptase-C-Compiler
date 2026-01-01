#define TEST_ZERO 0

int main(void) {
  int x = TEST_ZERO;
  int *p = &x;
  int *q = &x;
  return p + q;
}
