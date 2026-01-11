// Purpose: Exercise while, do-while, and for loops with break/continue.
// Expected: main returns 14.
int main(void) {
  int sum = 0;
  int i = 0;
  while (i < 5) {
    i = i + 1;
    if (i == 3) {
      continue;
    }
    sum = sum + i;
    if (i == 4) {
      break;
    }
  }

  int j = 0;
  do {
    j = j + 2;
  } while (j < 3);

  for (int k = 0; k < 3; k = k + 1) {
    sum = sum + k;
  }

  return sum + j;
}
