#define TEST_ZERO 0

void cleanup_ptr(int **p);

int main(void) {
  int value = TEST_ZERO;
  int *ptr __attribute__((cleanup(cleanup_ptr))) = &value;
  return *ptr;
}
