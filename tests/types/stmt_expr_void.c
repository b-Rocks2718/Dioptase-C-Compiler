void touch(int *p) {
  *p = 1;
}

int main(void) {
  int x = 0;
  ({
    int y = 2;
    if (y) {
      touch(&x);
    }
  });
  return x;
}
