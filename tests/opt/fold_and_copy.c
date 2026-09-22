/*
 * 3 * 4 folds to a temporary, copy propagation substitutes that temporary
 * into the addition, and a later constant-folding pass folds 2 + 12.
 */

int main(void) {
  return 2 + 3 * 4;
}
