// Purpose: Verify the comma operator sequences side effects left-to-right.
// Expected: main returns K_START + K_STEP + K_STEP.
#define K_STEP 1 // increment step for sequencing check
#define K_START 0 // initial value before comma expression

// Purpose: Increment a value by kStep so sequencing is observable.
// Inputs: value points to the integer to bump.
// Outputs: Returns the updated value.
// Invariants/Assumptions: value is non-NULL.
static int bump(int *value) {
  *value = *value + K_STEP;
  return *value;
}

// Purpose: Evaluate a comma expression that sequences two increments.
// Inputs: None.
// Outputs: Returns the final value of x after the comma expression.
// Invariants/Assumptions: bump executes left-to-right within the comma operator.
int main(void) {
  int x = K_START;
  int result = (bump(&x), bump(&x), x);
  return result;
}
