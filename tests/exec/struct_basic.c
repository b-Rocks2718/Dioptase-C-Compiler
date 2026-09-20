struct Pair { /* Define the struct used by the struct basic test. */
  int a;
  int b;
};

int main(void) { /* Exercise struct basic behavior. */
  struct Pair p = {3, 7};
  p.a = p.a + 1;
  p.b = 10;
  return p.a + p.b;
}
