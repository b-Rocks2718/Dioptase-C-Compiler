int inc(int x) { /* Return the incremented value used by this test. */
  return x + 1;
}

int apply(int (*fn)(int), int value) { /* Invoke the callback with the supplied argument. */
  return (*fn)(value);
}

int main(void) { /* Exercise fun ptr param behavior. */
  int (*fp)(int) = inc;
  return apply(fp, 41);
}
