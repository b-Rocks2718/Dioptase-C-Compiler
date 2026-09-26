int main(void) { /* Exercise arith promotions behavior. */
  int x = 1;
  long y = 2;
  long z = x + y;
  return (int)z;
}
