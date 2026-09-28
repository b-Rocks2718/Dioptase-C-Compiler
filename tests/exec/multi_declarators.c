// Verify declarations with several declarators (`int a, b;`) behave like
// separate declarations: shared specifiers, per-declarator pointer/array
// derivations and initializers, struct types, function prototypes, cleanup
// attributes (shared and per-declarator), and multi-declarator for-loop
// initializers, whose cleanups must run when the loop exits.
// Expected: main returns 0.
#define TEST_OK 0
#define TEST_FAIL 1

struct P { int x; int y; };

int g1 = 5, *g2, g3[2] = {1, 7};
int add(int x, int y), twice(int x);

int cleaned = 0;

void cleanup_add(int *p) { /* Accumulate cleaned-up values. */
  cleaned += *p;
}

int main(void) { /* Exercise multiple declarators per declaration. */
  g2 = &g1;
  if (*g2 != 5 || g3[1] != 7) return TEST_FAIL;

  int a = 2, b = a + 1, *c = &b, arr[2] = {a, b};
  if (b != 3 || *c != 3 || arr[0] != 2 || arr[1] != 3) return TEST_FAIL;

  struct P p1, p2;
  p1.x = a;
  p2.x = b;
  if (p1.x + p2.x != 5) return TEST_FAIL;

  if (add(a, b) != 5 || twice(a) != 4) return TEST_FAIL;

  int sum = 0;
  for (int i = 0, j = i + 10; i < j; i++, j--) {
    sum += i;
  }
  if (sum != 0 + 1 + 2 + 3 + 4) return TEST_FAIL;

  // Attributes before the declarators apply to all of them.
  {
    __attribute__((cleanup(cleanup_add))) int s1 = 2, s2 = 3;
  }
  if (cleaned != 2 + 3) return TEST_FAIL;

  // An attribute after a declarator applies only to that declarator.
  cleaned = 0;
  {
    int n1 = 2, n2 __attribute__((cleanup(cleanup_add))) = 9;
  }
  if (cleaned != 9) return TEST_FAIL;

  cleaned = 0;
  for (__attribute__((cleanup(cleanup_add))) int i = 5, j = 10; i < 0; i++) {
    return TEST_FAIL;
  }
  if (cleaned != 5 + 10) return TEST_FAIL;

  return TEST_OK;
}

int add(int x, int y) { return x + y; }
int twice(int x) { return 2 * x; }
