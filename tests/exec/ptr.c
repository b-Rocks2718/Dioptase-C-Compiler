int main(void) {
  int* x = 0;
  int y;
  int* z = &y;
  if (x == z) {
    return 1;
  } else if (x > z) {
    return 2;
  } else {
    return 0;
  }
}