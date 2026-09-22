int main(void) { /* A pointer conversion cannot drop const. */
  const int a = 1;
  int *p = &a;
  return *p;
}
