// Purpose: Basic sanity checks for unary plus on literals and variables.
// Expected: main returns K_LIT + K_VAR.
#define K_LIT 7 // literal to verify unary plus preserves value
#define K_VAR 5 // variable value to verify unary plus preserves value

int main(void) {
  int x = K_VAR;
  int result = (+K_LIT) + (+x);
  return result;
}
