// A frame larger than the 12-bit ALU immediate (2048 bytes) is allocated with
// movi + register sub instead of `add sp sp -N`, and taking the address of a
// local that far below bp needs the same fallback. Check that both ends of the
// big frame are usable and that writing them leaves the caller's locals intact.
// Expected: main returns 0.
int touch_ends(int seed) {
  int buf[520];
  buf[0] = seed;
  buf[519] = seed + 1;
  return buf[0] + buf[519];
}

int main(void) {
  int guard_low = 11;
  int guard_high = 22;

  if (touch_ends(5) != 5 + 6) {
    return 1;
  }
  if (guard_low != 11 || guard_high != 22) {
    return 2;
  }
  return 0;
}
