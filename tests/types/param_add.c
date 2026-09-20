#define TEST_ONE 1
#define TEST_TWO 2

int add(int a, int b) { /* Add the arguments for this arithmetic test. */
  return a + b;
}

int main(void) { /* Exercise param add behavior. */
  return add(TEST_ONE, TEST_TWO);
}
