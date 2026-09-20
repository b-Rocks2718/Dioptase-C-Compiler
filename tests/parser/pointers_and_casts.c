int main(void){ /* Exercise pointers and casts behavior. */
  int x = 1;
  int *p = &x;
  p = &x;
  x = (int) x;
  return *p;
}
