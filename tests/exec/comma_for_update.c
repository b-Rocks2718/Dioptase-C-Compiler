// Purpose: Verify comma operator sequencing in the for-loop update expression.
// Expected: main returns the sum of values assigned to sum during updates.
#define K_INIT 0 // initial value for loop variables
#define K_LIMIT 3 // loop stops when i reaches this bound
#define K_STEP 1 // increment applied to i each iteration

// Purpose: Accumulate loop values using a comma expression in the update clause.
// Inputs: None.
// Outputs: Returns the final sum computed in the update clause.
// Invariants/Assumptions: None.
int main(void) {
  int i = K_INIT;
  int sum = K_INIT;
  for (i = K_INIT, sum = K_INIT; i < K_LIMIT; i = i + K_STEP, sum = sum + i) {
  }
  return sum;
}
