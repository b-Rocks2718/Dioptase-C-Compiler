// Purpose: Verify cleanup runs for nested scopes with shadowed names.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1
#define OUTER_VALUE 5
#define INNER_VALUE 7
#define EXPECTED_SUM 12

int sum = 0;

void cleanup_add(int *p) {
  sum += *p;
}

int main(void) {
  {
    int value __attribute__((cleanup(cleanup_add))) = OUTER_VALUE;
    {
      int value __attribute__((cleanup(cleanup_add))) = INNER_VALUE;
    }
  }

  if (sum != EXPECTED_SUM) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
