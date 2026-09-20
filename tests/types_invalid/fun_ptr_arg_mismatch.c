int sum(int a, int b) { /* Add the arguments for the function-pointer mismatch test. */
  return a + b;
}

int main(void) { /* Exercise fun ptr arg mismatch behavior. */
  int (*fp)(int, int) = sum;
  return fp(1);
}
