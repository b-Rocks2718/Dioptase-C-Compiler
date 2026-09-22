/*
 * Copy propagation forwards the constant into the return, then dead-store
 * elimination deletes the copies that no longer have a live destination.
 */

int main(void) {
  int a = 5;
  int b = a;
  return b;
}
