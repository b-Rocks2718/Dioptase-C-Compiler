int f(int * const p) { /* Top-level const on a parameter applies in the body. */
  p = 0;
  return 1;
}

int main(void) {
  return f(0);
}
