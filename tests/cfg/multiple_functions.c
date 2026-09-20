/*
 * Verifies that CFG visualization emits one independently numbered graph for
 * each function definition.
 */

static int helper(void) {
  return 7;
}

int main(void) { /* Exercise multiple functions behavior. */
  return helper();
}
