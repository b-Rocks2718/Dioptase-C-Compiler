// Basic sanity checks for unary plus on literals and variables.
// Expected: main returns 7 + 5.

int main(void) { /* Exercise unary plus basic behavior. */
  int x = 5;
  int result = (+7) + (+x);
  return result;
}
