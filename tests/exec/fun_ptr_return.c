int add1(int x) {
  return x + 1;
}

int sub1(int x) {
  return x - 1;
}

int (*choose(int use_add))(int) {
  if (use_add) {
    return add1;
  }
  return sub1;
}

int main(void) {
  int (*fn)(int) = choose(0);
  return fn(10);
}
