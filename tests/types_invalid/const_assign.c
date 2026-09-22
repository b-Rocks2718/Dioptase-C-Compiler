int main(void) { /* Assignment cannot modify a const object. */
  const int a = 1;
  a = 2;
  return a;
}
