/*
 * Verifies that a backward jump is rendered as a self-edge and that the
 * unreachable return following the jump remains a separate block.
 */

int main(void) {
loop:
  goto loop;
  return 0;
}
