// Exercise static storage initialization and access.
// Expected: main returns 18.
static int counter = 3;

static int bump(void) { /* Increment the test state used by this fixture. */
  counter = counter + 4;
  return counter;
}

int main(void) { /* Exercise globals behavior. */
  int a = bump();
  int b = bump();
  return a + b;
}
