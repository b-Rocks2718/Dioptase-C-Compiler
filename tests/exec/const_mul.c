// A multiply by a literal that is not a power of two becomes shifts and
// adds/subtracts in asm_gen (multiply_by_constant) when the constant has at
// most four nonzero signed binary digits, and still calls the multiply
// builtin otherwise. Each product here is checked against the same multiply
// by a volatile copy of the constant, which always uses the builtin. Signed
// operands stay small enough not to overflow; unsigned ones cover
// wraparound. Also covers a literal on the left, a destination that aliases
// the source, compound assignment, and constants that fall back to the
// builtin (too many digits, or negative).
// Expected: main returns 0.
int main(void) {
  volatile int vals[7] = {0, 1, -1, 7, -7, 1000, -999};
  volatile int k3 = 3, k7 = 7, k12 = 12, k60 = 60, k100 = 100, k255 = 255;
  volatile int k768 = 768, k1000 = 1000, k30720 = 30720, k0x5555 = 0x5555, km3 = -3;
  volatile unsigned u3 = 3, uffff = 0xFFFF, u10001 = 0x10001, u7fffffff = 0x7FFFFFFF;
  for (int i = 0; i < 7; i++) {
    int x = vals[i];
    if (x * 3 != x * k3) return 1;
    if (x * 7 != x * k7) return 2;
    if (x * 12 != x * k12 || 12 * x != x * k12) return 3;
    if (x * 60 != x * k60) return 4;
    if (x * 100 != x * k100) return 5;
    if (x * 255 != x * k255) return 6;
    if (x * 768 != x * k768) return 7;
    if (x * 1000 != x * k1000) return 8;
    if (x * 30720 != x * k30720) return 9;
    if (x * 0x5555 != x * k0x5555) return 10;
    if (x * -3 != x * km3) return 11;

    int a = x;
    a = a * 12;
    int b = x;
    b *= 1000;
    if (a != x * k12 || b != x * k1000) return 12;

    unsigned u = (unsigned)x * 2654435761u;
    if (u * 3u != u * u3) return 13;
    if (u * 0xFFFFu != u * uffff) return 14;
    if (u * 0x10001u != u * u10001) return 15;
    if (u * 0x7FFFFFFFu != u * u7fffffff) return 16;
  }
  return 0;
}
