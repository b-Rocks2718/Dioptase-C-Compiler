int main(void) { /* Parse volatile on objects and on each pointer level. */
  int x = 1;
  volatile int y = 2;
  int * volatile p = &x;
  volatile int *q = &y;
  volatile int * volatile r = q;
  return *p + *q + *r;
}
