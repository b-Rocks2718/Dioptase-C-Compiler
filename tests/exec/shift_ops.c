// Shifts lower to the lsl/lsr/asr machine instructions. Check the edges of the
// defined range (counts 0 and 31), arithmetic vs logical right shift on a set
// sign bit, promotion of narrow operands, and compound assignment. Counts come
// from a volatile so the shifts are not constant-folded.
// Expected: main returns 0.
int main(void) {
  volatile int zero = 0;
  volatile int one = 1;
  volatile int four = 4;
  volatile int thirty_one = 31;

  int neg = -16;
  unsigned int high = 0x80000000u;
  unsigned char c = 255;

  if ((5 << zero) != 5 || (5 >> zero) != 5) {
    return 1;
  }
  if ((1 << thirty_one) != (int)0x80000000u) {
    return 2;
  }
  if ((neg >> four) != -1 || (neg >> one) != -8) {
    return 3;
  }
  if ((high >> thirty_one) != 1u || (high >> four) != 0x08000000u) {
    return 4;
  }
  if (((int)high >> thirty_one) != -1) {
    return 5;
  }
  // c promotes to int, so bits shifted past bit 7 are kept.
  if ((c << four) != 0xFF0) {
    return 6;
  }
  int x = 3;
  x <<= four;
  x >>= one;
  if (x != 24) {
    return 7;
  }
  return 0;
}
