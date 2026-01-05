// Purpose: Exercise switch/case lowering and default handling.
// Expected: main returns 20.
int main(void) {
  int x = 2;
  int y = 0;
  switch (x) {
    case 1:
      y = 10;
      break;
    case 2:
      y = 20;
      break;
    default:
      y = 30;
      break;
  }
  return y;
}
