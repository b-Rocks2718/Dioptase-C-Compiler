/*
 * Volatile reads and writes survive optimization. An unused read is a side
 * effect, a later write does not delete an earlier write, and copy propagation
 * must not turn a later use into another read of the volatile object.
 * A plain dead store in the same function is still removed.
 */

static volatile int kept;

struct Box {
  volatile int m;
};

struct Plain {
  int n;
};

int volatile_aggregate(void) {
  volatile struct Plain p = {6};
  return p.n;
}

int main(void) {
  volatile int x = 1;
  x = 2;
  x;
  int dead = 1;
  dead = 2;
  int a = 4;
  int b = a;
  int seen = x;
  int copied = seen;
  volatile int *p = &x;
  *p = 3;
  int y = *p;
  struct Box s;
  s.m = b;
  int z = s.m;
  kept = y;
  int n = 5;
  int * volatile vp = &n;
  int *q = vp;
  return copied + z + *q;
}
