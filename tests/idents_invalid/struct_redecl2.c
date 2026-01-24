
struct Test {
  int a;
};

int main() {
  union Test t;
  t.a = 5;
  return 0;
}
