// Purpose: Exercise arithmetic, bitwise, shift, and unary ops for TAC lowering.
// Expected: main returns 250.
int main(void) {
  int a = 6;
  int b = 3;
  int c = a + b;
  c = c * 2;
  c = c - 4;
  c = c / 2;
  c = c % 5;
  c = c | 8;
  c = c & 7;
  c = c ^ 5;
  c = -c;
  c = c + 12;
  c = ~c;
  c = c & 0xFF;
  return c;
}
