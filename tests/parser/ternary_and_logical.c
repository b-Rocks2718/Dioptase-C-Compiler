int main(void){ /* Exercise ternary and logical behavior. */
  int x = 1;
  int y = 2;
  return x < y && y < 5 ? x | y : x ^ y;
}
