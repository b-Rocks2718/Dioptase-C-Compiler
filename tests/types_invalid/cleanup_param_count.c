#define TEST_ZERO 0

void cleanup_two(int *a, int *b);

int main(void) {
  int value __attribute__((cleanup(cleanup_two))) = TEST_ZERO;
  return value;
}
