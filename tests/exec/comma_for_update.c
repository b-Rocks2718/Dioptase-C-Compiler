// Verify comma operator sequencing in the for-loop update expression.
// Expected: main returns 1 + 2 + 3, the values assigned to sum during updates.

// Accumulate loop values using a comma expression in the update clause.
int main(void) {
  int i = 0;
  int sum = 0;
  for (i = 0, sum = 0; i < 3; i = i + 1, sum = sum + i) {
  }
  return sum;
}
