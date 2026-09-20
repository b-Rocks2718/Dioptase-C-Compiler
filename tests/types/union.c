
union Test;

union Test { /* Define the struct used by the union test. */
  int a[3];
  short b;
};

union Test g = { {0, 1} };

int main(){ /* Exercise union behavior. */
  union Test t = { {3} };
  union Test* p = &t;
  union Test u = t;
  p->a[0] = 5;
  p->b = 10;
  t.a[0] = 5;
  t.b = 10;
  int sum = t.a[0] + t.b;
  return sum;
}

void func(union Test u, union Test* v){ /* Copy the aggregate arguments to exercise parameter passing. */
  return;
}
