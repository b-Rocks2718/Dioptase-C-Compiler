// A leaf function that only reads a stack-passed parameter still needs a
// frame: incoming stack args are addressed as [bp, 8+], so skipping the
// prologue would read through the caller's bp. Guards the frameless-leaf
// check against dropping the frame once params stay in registers.
// Expected: main returns 0.
int ninth(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
  return i;
}

int tenth(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
  return j;
}

int main(void) {
  if (ninth(1, 2, 3, 4, 5, 6, 7, 8, 9) != 9) {
    return 1;
  }
  if (tenth(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 10) {
    return 2;
  }
  return 0;
}
