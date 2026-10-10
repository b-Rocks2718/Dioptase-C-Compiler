// Multiply, divide, and modulo by a power-of-two literal are strength-reduced
// to shifts and masks in asm_gen. Signed divide and modulo must still truncate
// toward zero like C, so negative dividends are the cases that matter: a bare
// `asr`/`and` would give -1 / 2 == -1 and -3 % 4 == 1. Operands are volatile so
// nothing is constant-folded. Also covers a destination that aliases the
// dividend, a literal on the left of a multiply, and divisor 1 (shift by 0).
// Expected: main returns 0.
int main(void) {
  volatile int vals[17] = {0, 1, -1, 2, -2, 3, -3, 7, -7, 8, -8, 9, -9, 1000, -1000,
                         2147483647, -2147483647 - 1};
  int n = (int)(sizeof(vals) / sizeof(vals[0]));
  for (int i = 0; i < n; i++) {
    int x = vals[i];
    unsigned u = (unsigned)x;
    volatile int div1 = 1;

    if (x / 1 != x || x % 1 != 0 || x * 1 != x) return 1;
    if (x / 2 != x / (div1 + 1)) return 2;
    if (x % 2 != x % (div1 + 1)) return 3;
    if (x / 4 != x / (div1 * 4) || x % 4 != x % (div1 * 4)) return 4;
    if (x / 8 != x / (div1 * 8) || x % 8 != x % (div1 * 8)) return 5;
    if (x / 4096 != x / (div1 * 4096) || x % 4096 != x % (div1 * 4096)) return 6;
    if (x / 0x40000000 != x / (div1 * 0x40000000) ||
        x % 0x40000000 != x % (div1 * 0x40000000)) return 7;

    if (x * 2 != x + x || x * 16 != x << 4 || 8 * x != x << 3) return 8;

    if (u / 2 != u >> 1 || u % 2 != (u & 1) || u / 16 != u >> 4 || u % 16 != (u & 15)) return 9;
    if (u * 32 != u << 5 || 32 * u != u << 5) return 10;

    int a = x;
    a = a / 4;
    int b = x;
    b = b % 8;
    int c = x;
    c /= 2;
    int d = x;
    d %= 2;
    if (a != x / (div1 * 4) || b != x % (div1 * 8) || c != x / (div1 + 1) ||
        d != x % (div1 + 1)) return 11;
  }

  volatile int m = -3;
  if (m / 2 != -1 || m % 4 != -3 || -1 / (volatile int)2 != 0 || m / 4 != 0) return 12;
  return 0;
}
