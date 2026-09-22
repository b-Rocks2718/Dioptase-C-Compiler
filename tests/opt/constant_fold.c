/*
 * Constant folding replaces constant arithmetic, unary ops, and a narrowing
 * cast with copies. if (0) becomes an unconditional jump and if (1) drops
 * its comparison. Other passes stay off, so both sides of each branch remain.
 */

int main(void) {
  int sum = 2 + 3;
  int product = 6 * 7;
  int shifted = 1 << 3;
  int masked = 15 & 7;
  int negated = -4;
  int complemented = ~1;
  int narrowed = (char)0x123;
  if (0) {
    return 1;
  }
  return sum;
}

int taken(void) {
  if (1) {
    return 3;
  }
  return 4;
}
