unsigned int add(unsigned int a, int b) {
  return a + b;
}

int main(void) {
  unsigned int u = 1;
  int s = -2;
  unsigned int r = add(u, s);
  return r;
}
