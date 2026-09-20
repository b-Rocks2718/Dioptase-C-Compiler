unsigned int add(unsigned int a, int b) { /* Add the arguments for this arithmetic test. */
  return a + b;
}

int main(void) { /* Exercise unsigned arith behavior. */
  unsigned int u = 1;
  int s = -2;
  unsigned int r = add(u, s);
  return r;
}
