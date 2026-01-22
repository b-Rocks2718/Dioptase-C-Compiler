
union Test {
  int a;
  short b;
};

int main(){
  union Test t;
  union Test* p = &t;
  p->a = 5;
  p->b = 10;
  t.a = 5;
  t.b = 10;
  int sum = t.a + t.b;
  return sum;
}
