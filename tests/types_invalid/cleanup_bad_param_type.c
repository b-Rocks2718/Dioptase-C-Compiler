#define TEST_ZERO 0

void cleanup_short(short *p);

int main(void) { /* Exercise cleanup bad param type behavior. */
  int value __attribute__((cleanup(cleanup_short))) = TEST_ZERO;
  return value;
}
