int main(void) {
  int base = 3;
  int result = 1 + ({ int t = base * 2; t + 4; });
  return result;
}
