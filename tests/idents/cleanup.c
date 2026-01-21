#define TEST_ZERO 0

void cleanup_int(int *p);

int main(void) {
  __attribute__((cleanup(cleanup_int))) int value = TEST_ZERO;
  int __attribute__((cleanup(cleanup_int))) value2 = TEST_ZERO;
  int value3 __attribute__((cleanup(cleanup_int))) = TEST_ZERO;
  return value;
}
