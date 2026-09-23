/*
Direct tail calls: self recursion with an accumulator, mutual recursion,
argument permutation (the new args are computed from the old ones), a call
that fills all eight argument registers, and a call with stack arguments,
which is lowered as call + return instead of a real tail call.
Returns the number of checks that produced the expected value.
*/

int sum_to(int n, int acc) { /* Accumulate n + (n-1) + ... + 1 via self tail calls. */
  if (n == 0) {
    return acc;
  }
  return sum_to(n - 1, acc + n);
}

int is_even(int n);

int is_odd(int n) { /* Mutually tail-recursive parity check. */
  if (n == 0) {
    return 0;
  }
  return is_even(n - 1);
}

int is_even(int n) { /* Mutually tail-recursive parity check. */
  if (n == 0) {
    return 1;
  }
  return is_odd(n - 1);
}

int sub(int a, int b) { /* Order-sensitive helper for the permutation check. */
  return a - b;
}

int flip_sub(int a, int b) { /* Tail call with the argument registers swapped. */
  return sub(b, a);
}

int mix8(int a, int b, int c, int d, int e, int f, int g, int h) { /* Weight each register argument differently. */
  return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}

int call8(int x) { /* Tail call using all eight argument registers. */
  return mix8(x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7);
}

int mix10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { /* Two of these arguments go on the stack. */
  return a + b + c + d + e + f + g + h + i * 10 + j * 100;
}

int call10(int x) { /* Stack arguments force the call + return fallback. */
  return mix10(x, x, x, x, x, x, x, x, x + 1, x + 2);
}

int main(void) { /* Exercise direct tail call behavior. */
  int passed = 0;
  passed += sum_to(100, 0) == 5050;
  passed += is_even(51) == 0;
  passed += is_odd(51) == 1;
  passed += flip_sub(3, 10) == 7;
  passed += call8(1) == 204;
  passed += call10(1) == 328;
  return passed;
}
