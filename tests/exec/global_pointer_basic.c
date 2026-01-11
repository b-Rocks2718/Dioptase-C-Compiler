// Purpose: Exercise a global pointer that targets a global integer.
// Expected: main returns 7.
int g = 1;
int *gp;

int main(void) {
  gp = &g;
  *gp = 7;
  return g;
}
