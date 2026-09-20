// Verify cleanup runs for a for-init variable after the loop.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1
#define INIT_VALUE 7
#define LIMIT 0

int result = 0;

void cleanup_capture(int *p) { /* Record cleanup of the captured object. */
  result = *p;
}

int main(void) { /* Exercise cleanup for init behavior. */
  for (int i __attribute__((cleanup(cleanup_capture))) = INIT_VALUE;
       i < LIMIT;
       i = i + 1) {
    result = TEST_FAIL;
  }

  if (result != INIT_VALUE) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
