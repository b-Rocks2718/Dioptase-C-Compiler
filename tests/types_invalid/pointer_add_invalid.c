int main(void) { /* Exercise pointer add invalid behavior. */
  int x = 0;
  int *p = &x;
  int *q = &x;
  return p + q;
}
