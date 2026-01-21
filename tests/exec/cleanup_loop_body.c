// Purpose: Verify cleanup runs once per loop iteration for block locals.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1
#define COUNT 3
#define BASE 10
#define EXPECTED_SUM 33

int sum = 0;

void cleanup_add(int *p) {
  sum += *p;
}

int main(void) {
  for (int i = 0; i < COUNT; i = i + 1) {
    int value __attribute__((cleanup(cleanup_add))) = BASE + i;
  }

  if (sum != EXPECTED_SUM) {
    return TEST_FAIL;
  }

  return TEST_OK;
}
