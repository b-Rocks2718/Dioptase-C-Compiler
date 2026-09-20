
union Test { /* Define the struct used by the union test. */
  int a;
  short b;
};

int main(){ /* Exercise union behavior. */
  union Test t;
  union Test* p = &t;
  p->a = 5;
  p->b = 10;
  t.a = 5;
  t.b = 10;
  int sum = t.a + t.b;
  return sum;
}
