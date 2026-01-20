int add_one(int x) {
  return x + 1;
}

int main(void) {
  int (*fp)(int) = add_one;
  int y = fp(41);
  return y;
}
