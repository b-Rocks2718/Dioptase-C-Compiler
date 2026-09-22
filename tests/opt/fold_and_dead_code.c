/*
 * Constant folding deletes the never-taken branch of if (0), and dead-code
 * elimination then removes that block and the label that only fell through.
 */

int main(void) {
  if (0) {
    return 1;
  }
  return 2;
}
