// Ensure unary plus is neutral inside compound expressions.
// Expected: main returns 2 + 3.

static int bump(int value) { /* Increment the test state used by this fixture. */
  return value + 3;
}

int main(void) { /* Exercise unary plus sequence behavior. */
  int x = 2;
  x = +bump(x);
  return +x;
}
