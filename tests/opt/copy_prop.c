/*
 * Copy propagation rewrites later uses to the copied source and deletes a
 * copy that only restates a reaching copy. A store through a pointer kills
 * copies of the address-taken variable, so aliased still returns a.
 */

int propagate(void) {
  int a = 4;
  int b = a;
  int c = b + 1;
  return c;
}

int redundant(void) {
  int a = 4;
  int b = a;
  a = b;
  return a;
}

int aliased(void) {
  int a = 1;
  int b = a;
  int *p = &a;
  *p = 2;
  return a;
}
