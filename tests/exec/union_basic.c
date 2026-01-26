union U {
  int a;
  int b;
};

int main(void) {
  union U u = {5};
  u.a = u.a + 2;
  return u.a;
}
