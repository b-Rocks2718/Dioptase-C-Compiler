// Purpose: Exercise address-of, dereference, loads, and stores.
// Expected: main returns 18.
int main(void) {
  int x = 3;
  int *p = &x;
  *p = 9;
  return x + *p;
}
