int inc(int x) { /* Return the incremented value used by this test. */
  return x + 1;
}

int main(void) { /* Exercise fun ptr ptr behavior. */
  int (*fp)(int) = inc;
  int (**pp)(int) = &fp;
  return (*pp)(41);
}
