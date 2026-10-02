/*
 * `inline` is accepted as a function specifier and otherwise ignored, so
 * inline functions behave exactly like ordinary ones: they can be repeated,
 * recursive, declared inline after their definition, and called by pointer.
 */

static inline int add(int a, int b) {
  return a + b;
}

static int late(int x) { return x - 2; }
static inline int late(int x);

inline inline static int fact(int n) {
  if (n <= 1) {
    return 1;
  }
  return n * fact(n - 1);
}

int main(void) {
  int (*fp)(int, int) = add;
  return add(5, 7) + late(10) + fact(4) + fp(1, 2);
}
