
struct Test {
  int a;
};

int main() {
  {
    struct Test {
      short b;
    };
    struct Test t2;
    t2.b = 3;
  }

  struct Test t;
  t.a = 5;
  return 0;
}
