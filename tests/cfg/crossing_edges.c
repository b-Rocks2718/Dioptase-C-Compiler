/*
 * Verifies that independent crossing edges are visibly distinct from real
 * corners and junctions in the layered ASCII graph.
 */

int main(void) {
source_one:
  goto target_two;
source_two:
  goto target_one;
target_one:
  return 1;
target_two:
  return 2;
}
