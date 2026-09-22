int main(void) { /* A pointer conversion cannot drop volatile. */
  volatile int a = 1;
  int *p = &a;
  return *p;
}
