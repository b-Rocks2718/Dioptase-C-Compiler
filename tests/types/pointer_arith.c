int main(void) { /* Exercise pointer arith behavior. */
  int x = 1;
  int y = 2;
  int *p = &x;
  int *q = &y;
  p = p + 1;
  q = p - 1;
  return *q;
}
