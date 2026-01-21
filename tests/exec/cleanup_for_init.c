// Purpose: Verify cleanup runs for a for-init variable after the loop.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1
#define INIT_VALUE 7
#define LIMIT 0

int result = 0;

void cleanup_capture(int *p) {
  result = *p;
}

int main(void) {
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
