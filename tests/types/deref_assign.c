int main(void) { /* Exercise deref assign behavior. */
  int x = 1;
  int *p = &x;
  *p = 9;
  return x;
}
