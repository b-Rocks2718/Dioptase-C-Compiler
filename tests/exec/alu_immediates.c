// Binary ops with a literal right operand use the ALU immediate form when the
// value fits the encoding (docs/ISA.md) and fall back to a register otherwise.
// Check values on both sides of each boundary: 12-bit signed add, sub lowered
// as `add -imm` (2048 does not fit after negation), one-byte bitwise lanes, and
// the 5-bit shift amount. x is volatile so nothing is constant-folded.
// Expected: main returns 0.
int main(void) {
  volatile int vx = 1000;
  int x = vx;

  if (x + 2047 != 3047 || x + 2048 != 3048 || x + -2048 != -1048 || x + -2049 != -1049) {
    return 1;
  }
  if (x - 3 != 997 || x - 2048 != -1048 || x - 2049 != -1049 || x - -2047 != 3047 ||
      x - -2048 != 3048) {
    return 2;
  }

  int bits = vx * 0 + 0x12345678;
  if ((bits & 0xFF) != 0x78 || (bits & 0xFF00) != 0x5600 || (bits & 0x7F000000) != 0x12000000 ||
      (bits & 0x1FF) != 0x78) {
    return 3;
  }
  if ((bits | 0xF0) != 0x123456F8 || (bits ^ 0xFF0000) != 0x12CB5678 ||
      (bits ^ (int)0xFF000000u) != (int)0xED345678u) {
    return 4;
  }

  if ((x << 3) != 8000 || (x >> 3) != 125 || ((-x) >> 31) != -1 ||
      ((unsigned)-x >> 31) != 1u || (1 << 31) != (int)0x80000000u) {
    return 5;
  }
  return 0;
}
