
union Test;

union Test { /* Define the struct used by the union test. */
  int a;
  short b;
};

int main(){ /* Exercise union behavior. */
  union Test t = { 0 };
  union Test* p = &t;
  p->a = 5;
  p->b = 10;
  t.a = 5;
  t.b = 10;
  int sum = t.a + t.b;
  return sum;
}

void func(union Test u, union Test* v){ /* Copy the aggregate arguments to exercise parameter passing. */
  return;
}
