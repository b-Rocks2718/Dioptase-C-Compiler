
struct Test {
  int a;
  short b;
};

int main() {
  struct Test t;
  t.c = 5; // Error: 'c' is not a member of 'struct Test'
  return 0;
}
