#define TEST_VALUE 7

int deref(int *p) { /* Read through the pointer parameter. */
  return *p;
}

int main(void) { /* Exercise param pointer behavior. */
  int x = TEST_VALUE;
  return deref(&x);
}
