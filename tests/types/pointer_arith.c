#define TEST_ONE 1
#define TEST_TWO 2

int main(void) {
  int x = TEST_ONE;
  int y = TEST_TWO;
  int *p = &x;
  int *q = &y;
  p = p + TEST_ONE;
  q = p - TEST_ONE;
  return *q;
}
