// Const objects and pointers stay readable after type checking.
// Expected: main returns 21.
int sum(const int *xs, int n) {
  int total = 0;
  int i = 0;
  while (i < n) {
    total = total + xs[i];
    i = i + 1;
  }
  return total;
}

int main(void) {
  const int xs[3] = {4, 5, 6};
  int y = 6;
  int * const py = &y;
  return sum(xs, 3) + *py;
}
