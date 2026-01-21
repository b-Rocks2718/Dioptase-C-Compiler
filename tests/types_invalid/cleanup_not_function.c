#define TEST_ZERO 0

int cleanup_target;

int main(void) {
  int value __attribute__((cleanup(cleanup_target))) = TEST_ZERO;
  return value;
}
