extern int helper(void);

int helper(void) { /* Provide the helper function referenced by this test. */
  return 7;
}

int main(void) { /* Exercise linkage extern function behavior. */
  return helper();
}
