// Verify cleanup runs once per loop iteration for block locals.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1

int sum = 0;

void cleanup_add(int *p) { /* Add the cleaned-up value to the test total. */
  sum += *p;
}

int main(void) { /* Exercise cleanup loop body behavior. */
  for (int i = 0; i < 3; i = i + 1) {
    int value __attribute__((cleanup(cleanup_add))) = 10 + i;
  }

  if (sum != 10 + 11 + 12) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
