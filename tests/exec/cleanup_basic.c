// Verify cleanup runs at block exit for a local variable.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1

int result = 0;

void cleanup_int(int *p) { /* Record cleanup of the integer object. */
  result = *p;
}

int main(void) { /* Exercise cleanup basic behavior. */
  {
    int value __attribute__((cleanup(cleanup_int))) = 37;
  }

  if (result != 37) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
