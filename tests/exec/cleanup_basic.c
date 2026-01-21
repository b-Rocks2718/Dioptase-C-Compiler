// Purpose: Verify cleanup runs at block exit for a local variable.
// Expected: main returns 0.
#define TEST_ZERO 0
#define TEST_OK 0
#define TEST_FAIL 1
#define TEST_VALUE 37

int result = TEST_ZERO;

void cleanup_int(int *p) {
  result = *p;
}

int main(void) {
  {
    int value __attribute__((cleanup(cleanup_int))) = TEST_VALUE;
  }

  if (result != TEST_VALUE) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
