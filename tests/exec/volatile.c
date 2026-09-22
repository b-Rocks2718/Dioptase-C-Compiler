// Each volatile access reads or writes the current value of the object.
// Expected: main returns 40.
struct Box {
  volatile int m;
};

struct Plain {
  int n;
};

int main(void) {
  volatile int x = 1;
  x = x + 2;
  x += 4;
  int old = x++;
  volatile int *p = &x;
  *p = *p + 1;
  int n = 6;
  int * volatile q = &n;
  *q = *q + 1;
  struct Box s;
  s.m = 5;
  int from_member = s.m;
  volatile struct Plain whole = {1};
  int from_whole = whole.n;
  int a = 10;
  volatile int *pv = &a;
  *pv = 11;
  return old + *p + *q + from_member + from_whole + *pv;
}
