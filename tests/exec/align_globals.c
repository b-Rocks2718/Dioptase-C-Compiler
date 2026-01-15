// Purpose: Check alignment of interleaved short/int globals.
// Expected: main returns 0.
short g_short_a = 1;
int g_int_a = 2;
short g_short_b = 3;
short g_short_c = 3;
int g_int_b = 4;

int main(void) {
  unsigned int align_short = 2;
  unsigned int align_int = 4;

  if (((unsigned int)&g_short_a) % align_short != 0) {
    return 1;
  }
  if (((unsigned int)&g_int_a) % align_int != 0) {
    return 2;
  }
  if (((unsigned int)&g_short_b) % align_short != 0) {
    return 3;
  }
  if (((unsigned int)&g_int_b) % align_int != 0) {
    return 4;
  }

  return 0;
}
