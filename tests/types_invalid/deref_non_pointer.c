#define TEST_ONE 1

int main(void) { /* Exercise the invalid dereference in this test case. */
  int x = TEST_ONE;
  return *x;
}
