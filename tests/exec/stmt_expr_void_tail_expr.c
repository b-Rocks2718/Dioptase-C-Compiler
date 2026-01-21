void touch(int *p) {
  *p += 3;
}

int main(void) {
  int x = 4;
  ({
    int y = 1;
    x += y;
    touch(&x);
  });
  return x;
}
