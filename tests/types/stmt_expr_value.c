int main(void) { /* Exercise stmt expr value behavior. */
  int base = 3;
  int result = 1 + ({ int t = base * 2; t + 4; });
  return result;
}
