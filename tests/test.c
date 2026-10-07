// scratchpad for random tests and experiments

int next_collatz(int x) {
  if (x % 2 == 0)
    return x / 2;
  else
    return 3 * x + 1;
}

int main() {
  int a0 = 37;
  int a1 = next_collatz(a0); // 37 -> 112
  int a2 = next_collatz(a1); // 112 -> 56
  int a3 = next_collatz(a2); // 56 -> 28
  int a4 = next_collatz(a3); // 28 -> 14
  int a5 = next_collatz(a4); // 14 -> 7
  return a5;
}
