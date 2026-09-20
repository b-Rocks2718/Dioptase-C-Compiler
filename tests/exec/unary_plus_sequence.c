// Ensure unary plus is neutral inside compound expressions.
// Expected: main returns the final value of x after updates.
#define K_START 2 // starting value for x
#define K_ADD 3 // value added in the expression

static int bump(int value) { /* Increment the test state used by this fixture. */
  return value + K_ADD;
}

int main(void) { /* Exercise unary plus sequence behavior. */
  int x = K_START;
  x = +bump(x);
  return +x;
}
