// Purpose: Exercise dereference on both sides of an assignment.
// Expected: main returns 9.
int main(void) {
  int x = 4;
  int *p = &x;
  *p = *p + 5;
  return x;
}
