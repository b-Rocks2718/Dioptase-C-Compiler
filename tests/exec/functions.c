// Exercise function calls and recursion.
// Expected: main returns 17.
static int add(int a, int b) {
  return a + b;
}

static int fib(int n) { /* Compute the recursive Fibonacci value used by this test. */
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

int main(void) { /* Exercise functions behavior. */
  return add(5, 7) + fib(5);
}
