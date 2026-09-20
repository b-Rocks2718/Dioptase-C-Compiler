#define TEST_ZERO 0

void cleanup_int(int *p);

int main(void) { /* Exercise cleanup basic behavior. */
  int value __attribute__((cleanup(cleanup_int))) = TEST_ZERO;
  return value;
}
