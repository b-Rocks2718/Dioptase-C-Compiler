#define TEST_ZERO 0

int main(void) { /* Exercise undeclared cleanup behavior. */
  __attribute__((cleanup(cleanup_int))) int value = TEST_ZERO;
  return value;
}
