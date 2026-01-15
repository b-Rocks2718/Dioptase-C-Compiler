// Purpose: Check alignment of interleaved short/int function parameters.
// Expected: main returns 0.
static int check_params(short a, int b, short c, int d) {
  unsigned int align_short = 2;
  unsigned int align_int = 4;

  if (((unsigned int)&a) % align_short != 0) {
    return 1;
  }
  if (((unsigned int)&b) % align_int != 0) {
    return 2;
  }
  if (((unsigned int)&c) % align_short != 0) {
    return 3;
  }
  if (((unsigned int)&d) % align_int != 0) {
    return 4;
  }

  return 0;
}

int main(void) {
  return check_params(1, 2, 3, 4);
}
