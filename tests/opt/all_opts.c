/*
 * Every implemented pass runs together: constants fold, copies propagate,
 * the unused add is deleted, and the unreachable call is removed.
 */

int callee(void) {
  return 7;
}

int main(void) {
  int a = 2 * 3;
  int b = a;
  int unused = b + 4;
  if (0) {
    return callee();
  }
  return b;
}
