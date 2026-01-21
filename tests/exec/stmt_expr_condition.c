int main(void) {
  int x = 0;
  if (({ int y = 2; x = y; x; })) {
    x += 5;
  }
  return x;
}
