int sum(int a, int b) {
  return a + b;
}

int main(void) {
  int (*fp)(int, int) = sum;
  return fp(1);
}
