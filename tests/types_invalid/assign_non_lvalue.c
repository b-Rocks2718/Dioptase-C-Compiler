#define TEST_ONE 1
#define TEST_TWO 2

int main(void) { /* Exercise assign non lvalue behavior. */
  int x = TEST_ONE;
  (x + TEST_ONE) = TEST_TWO;
  return x;
}
