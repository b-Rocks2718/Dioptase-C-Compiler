/*
Statement expressions can hide loops and switches anywhere an expression may
appear. Labeling, goto resolution, and case collection must all find them,
including under unary operators and in expression statements; earlier
per-pass traversals missed both, leaving a loop unlabeled (internal error) and
a switch with no cases (its body was skipped).
*/
int main(void) {
  int x = -({ int i = 0; while (1) { i++; if (i == 3) break; } i; });
  if (x != -3) {
    return 1;
  }

  int r = 0;
  ({ switch (2) { case 1: r = 1; break; case 2: r = 5; break; } });
  if (r != 5) {
    return 2;
  }

  int y = -({ int j = 0; goto skip; j = 7; skip:; j + 4; });
  if (y != -4) {
    return 3;
  }
  return 0;
}
