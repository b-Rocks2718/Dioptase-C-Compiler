/*
 * Dead-store elimination removes writes whose destinations are not live,
 * including an address-taken local that is overwritten before it is read.
 * Calls and stores to static variables stay.
 */

static int kept;

int callee(void) {
  return 1;
}

int main(void) {
  int dead = 1 + 2;
  int live = 3;
  dead = live;
  kept = live;
  callee();
  return live;
}

int keep_addressed(void) {
  int a = 1;
  int *p = &a;
  a = 2;
  return *p;
}
