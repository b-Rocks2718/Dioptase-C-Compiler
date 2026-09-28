// Ensure the comma operator yields the rightmost expression in a larger expression.
// Expected: main returns 10 + 4 + 3.

// Exercise comma operator precedence inside a larger arithmetic expression.
int main(void) {
  int a = 0;
  int result = 10 + (a = 4, a + 3);
  return result;
}
