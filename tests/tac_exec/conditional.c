// Purpose: Exercise the conditional operator in expression lowering.
// Expected: main returns 10.
int main(void) {
  int x = 5;
  int y = (x > 3) ? 10 : 20;
  return y;
}
