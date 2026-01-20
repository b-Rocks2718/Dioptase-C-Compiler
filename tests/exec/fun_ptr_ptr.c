int inc(int x) {
  return x + 1;
}

int main(void) {
  int (*fp)(int) = inc;
  int (**pp)(int) = &fp;
  return (*pp)(41);
}
