
struct Test;

struct Test { /* Define the struct used by the struct test. */
  int a[3];
  short b;
};

struct Test g = {{0, 1} };

int main(){ /* Exercise struct behavior. */
  struct Test t = {{0} , 1};
  struct Test s = t;
  struct Test* p = &t;
  p->a[1] = 5;
  p->b = 10;
  t.a[1] = 5;
  t.b = 10;
  int sum = t.a[1] + t.b;
  return sum;
}

void func(struct Test u, struct Test* v){ /* Copy the aggregate arguments to exercise parameter passing. */
  return;
}
