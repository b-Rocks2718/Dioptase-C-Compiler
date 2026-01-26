struct Pair {
  int a;
  int b;
};

int main(void) {
  struct Pair p = {3, 7};
  p.a = p.a + 1;
  p.b = 10;
  return p.a + p.b;
}
