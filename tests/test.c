// scratchpad for random tests and experiments

int next_collatz(int x) {
  if (x % 2 == 0)
    return x / 2;
  else
    return 3 * x + 1;
}

int main() {
  int a0 = 37;
  int a1 = next_collatz(a0);
  int a2 = next_collatz(a1);
  int a3 = next_collatz(a2);
  int a4 = next_collatz(a3);
  int a5 = next_collatz(a4);
  return a5;
}
