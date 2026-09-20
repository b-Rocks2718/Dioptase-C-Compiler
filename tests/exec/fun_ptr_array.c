int inc(int x) { /* Return the incremented value used by this test. */
  return x + 1;
}

int dec(int x) { /* Return the decremented value used by this test. */
  return x - 1;
}

int main(void) { /* Exercise fun ptr array behavior. */
  int (*ops[2])(int) = {inc, dec};
  int a = ops[0](41);
  int b = ops[1](a);
  return b;
}
