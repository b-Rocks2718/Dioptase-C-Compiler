// Verify cleanup runs for multiple locals in the same scope.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1
#define TEST_A 5
#define TEST_B 9
#define TEST_SUM 14

int sum = 0;

void cleanup_add(int *p) { /* Add the cleaned-up value to the test total. */
  sum += *p;
}

int main(void) { /* Exercise cleanup two vars behavior. */
  {
    int first __attribute__((cleanup(cleanup_add))) = TEST_A;
    int second __attribute__((cleanup(cleanup_add))) = TEST_B;
  }

  if (sum != TEST_SUM) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
