// Purpose: Exercise static storage initialization and access.
// Expected: main returns 18.
static int counter = 3;

static int bump(void) {
  counter = counter + 4;
  return counter;
}

int main(void) {
  int a = bump();
  int b = bump();
  return a + b;
}
