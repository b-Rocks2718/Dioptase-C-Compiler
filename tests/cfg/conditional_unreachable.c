/*
 * Verifies conditional edges and preservation of unreachable TAC blocks.
 */

int main(void) {
  if (1) {
    return 1;
  } else {
    return 0;
  }
}
