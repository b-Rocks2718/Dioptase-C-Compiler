#define TEST_VALUE 7

int deref(int *p) {
  return *p;
}

int main(void) {
  int x = TEST_VALUE;
  return deref(&x);
}
