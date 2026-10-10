// scratchpad for random tests and experiments

int next_collatz(int x) {
  if (x % 2 == 0)
    return x / 2;
  else
    return 3 * x + 1;
}

int main() {
  int c0 = 101;
  int c1 = next_collatz(c0);
  int c2 = next_collatz(c1);
  int c3 = next_collatz(c2);
  int c4 = next_collatz(c3);
  int c5 = next_collatz(c4);
  return c5;
}
