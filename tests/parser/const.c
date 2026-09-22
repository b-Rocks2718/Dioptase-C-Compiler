int main(void) { /* Parse const on objects and on each pointer level. */
  int x = 1;
  const int y = 2;
  int * const p = &x;
  const int *q = &y;
  const int * const r = q;
  return *p + *q + *r;
}
