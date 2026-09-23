/*
Indirect tail calls through function pointers held in a parameter, in a
global, and chosen at run time, plus self recursion through a global
function pointer. The call target is read from the frame, so it must be
loaded before the frame is torn down.
Returns the number of checks that produced the expected value.
*/

int add3(int x) { /* Callback that adds three. */
  return x + 3;
}

int dbl(int x) { /* Callback that doubles. */
  return x * 2;
}

int (*g_op)(int);

int apply(int (*op)(int), int x) { /* Tail call through a parameter. */
  return op(x);
}

int apply_global(int x) { /* Tail call through a global. */
  return g_op(x);
}

int apply_chosen(int use_add, int x) { /* Tail call through a pointer selected at run time. */
  int (*op)(int) = use_add ? add3 : dbl;
  return op(x);
}

int (*g_step)(int, int);

int step(int n, int acc) { /* Self recursion through a global function pointer. */
  if (n == 0) {
    return acc;
  }
  return g_step(n - 1, acc + 2);
}

int main(void) { /* Exercise indirect tail call behavior. */
  int passed = 0;
  g_op = dbl;
  g_step = step;
  passed += apply(add3, 4) == 7;
  passed += apply(dbl, 4) == 8;
  passed += apply_global(21) == 42;
  passed += apply_chosen(1, 10) == 13;
  passed += apply_chosen(0, 10) == 20;
  passed += step(50, 0) == 100;
  return passed;
}
