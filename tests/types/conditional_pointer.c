int main(void) { /* Exercise conditional pointer behavior. */
  int x = 1;
  int y = 2;
  int *p = &x;
  int *q = &y;
  int *r = x ? p : 0;
  return *r;
}
