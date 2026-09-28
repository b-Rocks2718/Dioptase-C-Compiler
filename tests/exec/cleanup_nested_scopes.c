// Verify cleanup runs for nested scopes with shadowed names.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1

int sum = 0;

void cleanup_add(int *p) { /* Add the cleaned-up value to the test total. */
  sum += *p;
}

int main(void) { /* Exercise cleanup nested scopes behavior. */
  {
    int value __attribute__((cleanup(cleanup_add))) = 5;
    {
      int value __attribute__((cleanup(cleanup_add))) = 7;
    }
  }

  if (sum != 5 + 7) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
