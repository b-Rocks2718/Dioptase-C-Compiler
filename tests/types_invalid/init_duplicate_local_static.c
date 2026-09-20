int main(void) { /* Exercise init duplicate local static behavior. */
  static int counter = 1;
  static int counter = 2;
  return counter;
}
