const int g = 3;

void take_value(const int x);
void take_value(int x) {
  return;
}

int read_ptr(const int *p) {
  return *p;
}

struct Pair {
  const int x;
  int y;
};

int main(void) { /* Read const objects, pointers, and aggregate members. */
  const int a = 4;
  int b = 5;
  const int *p = &a;
  int *q = &b;
  const int *r = q;
  int * const cp = &b;
  const int xs[2] = {1, 2};
  struct Pair s;
  s.y = 9;
  const struct Pair cs = {7, 8};
  int n = cs.x + s.y;
  const int * const pp = &a;
  int i = 0;
  for (const int limit = 1; i < limit; i = i + 1) {
    n = n + xs[i];
  }
  take_value(a);
  return g + read_ptr(p) + *r + *cp + *pp + n;
}
