volatile int g = 3;

void take_value(volatile int x);
void take_value(int x) {
  return;
}

int read_ptr(volatile int *p) {
  return *p;
}

struct Pair {
  volatile int x;
  int y;
};

int main(void) { /* Read and write volatile objects, pointers, and members. */
  volatile int a = 4;
  int b = 5;
  volatile int *p = &a;
  int *q = &b;
  volatile int *r = q;
  int * volatile vp = &b;
  volatile int xs[2] = {1, 2};
  struct Pair s;
  s.x = 9;
  volatile struct Pair vs = {7, 8};
  int n = vs.x + s.x;
  volatile int * volatile pp = &a;
  int i = 0;
  for (volatile int limit = 1; i < limit; i = i + 1) {
    n = n + xs[i];
  }
  take_value(a);
  volatile int *common = i ? p : q;
  return g + read_ptr(p) + *r + *vp + *pp + n + *common;
}
