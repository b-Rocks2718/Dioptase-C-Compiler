int ping(int n){ /* Recurse to exercise calls before the base case. */
  if (n) return ping(n - 1);
  return 0;
}
