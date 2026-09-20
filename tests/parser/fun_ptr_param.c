int inc(int x){ /* Return the incremented value used by this test. */
  return x + 1;
}

int apply(int (*fn)(int), int x){ /* Invoke the callback with the supplied argument. */
  return (*fn)(x);
}
