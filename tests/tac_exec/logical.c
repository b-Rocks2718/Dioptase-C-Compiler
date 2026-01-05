// Purpose: Verify logical operators and short-circuit evaluation.
// Expected: main returns 14 (bump() is never called).
static int side_effect = 0;

static int bump(void) {
  side_effect = side_effect + 1;
  return 1;
}

int main(void) {
  int result = 0;
  if (0 && bump()) {
    result = 10;
  }
  if (1 || bump()) {
    result = result + 2;
  }
  if (!0) {
    result = result + 4;
  }
  if (5 > 3 && 2 < 3) {
    result = result + 8;
  }
  return result + side_effect;
}
