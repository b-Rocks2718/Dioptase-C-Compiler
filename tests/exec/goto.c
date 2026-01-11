// Purpose: Verify explicit label and goto lowering.
// Expected: main returns 2.
int main(void) {
  int x = 0;
  goto skip;
  x = 1;
skip:
  x = x + 2;
  return x;
}
