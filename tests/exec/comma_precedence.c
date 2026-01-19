// Purpose: Ensure the comma operator yields the rightmost expression in a larger expression.
// Expected: main returns K_PREFIX + K_ASSIGN + K_ADDEND.
#define K_INIT 0 // initial value for a before assignment
#define K_ASSIGN 4 // value assigned in comma expression
#define K_ADDEND 3 // value added after assignment
#define K_PREFIX 10 // value added outside the comma expression

// Purpose: Exercise comma operator precedence inside a larger arithmetic expression.
// Inputs: None.
// Outputs: Returns computed result of the comma expression plus kPrefix.
// Invariants/Assumptions: None.
int main(void) {
  int a = K_INIT;
  int result = K_PREFIX + (a = K_ASSIGN, a + K_ADDEND);
  return result;
}
