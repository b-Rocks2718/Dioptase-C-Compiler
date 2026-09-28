int foo(int a, int b) { /* Provide the cleanup callback used by this test. */
  return 0;
}

int main(void) { /* Exercise arg count mismatch behavior. */
  return foo(1);
}
