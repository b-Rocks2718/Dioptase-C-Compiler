// Purpose: Exercise function calls and recursion.
// Expected: main returns 17.
static int add(int a, int b) {
  return a + b;
}

static int fib(int n) {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

int main(void) {
  return add(5, 7) + fib(5);
}
