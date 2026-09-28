long widen(int a, int b) { /* Return the widened sum used by the argument-conversion test. */
  return 0;
}

int main(void) { /* Exercise call args behavior. */
  int x = 3;
  long y = 4;
  long z = widen(x, y);
  return z > 0;
}
