// Purpose: Check alignment of interleaved short/int locals.
// Expected: main returns 0.
int main(void) {
  short s1 = 1;
  int i1 = 2;
  short s2 = 3;
  int i2 = 4;

  unsigned int align_short = 2;
  unsigned int align_int = 4;

  if (((unsigned int)&s1) % align_short != 0) {
    return 1;
  }
  if (((unsigned int)&i1) % align_int != 0) {
    return 2;
  }
  if (((unsigned int)&s2) % align_short != 0) {
    return 3;
  }
  if (((unsigned int)&i2) % align_int != 0) {
    return 4;
  }

  return 0;
}
