struct P { int x; int y; };
int g1 = 1, *g2, g3[2] = {3, 4};
int f(int x), h(void);
struct P p1, p2;

int main(void){ /* Exercise multiple declarators per declaration. */
  int a = 2, b = a + 1, *c = &b;
  for (int i = 0, j = 10; i < j; i++) a += i;
  return a + b + *c;
}
