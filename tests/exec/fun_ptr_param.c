int inc(int x) {
  return x + 1;
}

int apply(int (*fn)(int), int value) {
  return (*fn)(value);
}

int main(void) {
  int (*fp)(int) = inc;
  return apply(fp, 41);
}
