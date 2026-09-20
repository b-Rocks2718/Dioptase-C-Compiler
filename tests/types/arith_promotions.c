#define TEST_ONE 1
#define TEST_TWO 2

int main(void) { /* Exercise arith promotions behavior. */
  int x = TEST_ONE;
  long y = TEST_TWO;
  long z = x + y;
  return (int)z;
}
