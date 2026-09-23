// Verify cleanup runs for multiple locals in the same scope.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1

int sum = 0;

void cleanup_add(int *p) { /* Add the cleaned-up value to the test total. */
  sum += *p;
}

int main(void) { /* Exercise cleanup two vars behavior. */
  {
    int first __attribute__((cleanup(cleanup_add))) = 5;
    int second __attribute__((cleanup(cleanup_add))) = 9;
  }

  if (sum != 5 + 9) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
