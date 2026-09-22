struct Pair {
  const int x;
  int y;
};

int main(void) { /* A const member is not a modifiable lvalue. */
  struct Pair s;
  s.x = 1;
  return s.x;
}
