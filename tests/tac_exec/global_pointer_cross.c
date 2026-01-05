// Purpose: Exercise global pointer mutation across function calls.
// Expected: main returns 7.
int g = 3;
int *gp;

static int init_ptr(void) {
  gp = &g;
  return 0;
}

static int bump_via_ptr(void) {
  *gp = *gp + 4;
  return g;
}

int main(void) {
  init_ptr();
  return bump_via_ptr();
}
