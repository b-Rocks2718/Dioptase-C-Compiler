// Verify the comma operator sequences side effects left-to-right.
// Expected: main returns 2.

// Increment a value so sequencing is observable.
// Returns the updated value.
static int bump(int *value) {
  *value = *value + 1;
  return *value;
}

// Evaluate a comma expression that sequences two increments.
// Returns the final value of x after the comma expression.
// bump executes left-to-right within the comma operator.
int main(void) {
  int x = 0;
  int result = (bump(&x), bump(&x), x);
  return result;
}
