/*
Static initializers are computed at compile time by the type checker's
constant evaluator rather than by generated code. This checks that the
evaluator follows the same C rules as run-time arithmetic: casts narrow and
re-extend values, results wrap at their type's width, comparisons use the
operands' converted type, narrow operands are promoted to int, and unary plus
is a constant expression. Each check returns a distinct code on failure.
*/
static int narrowed_sum = (char)200 + 1;
static char wrapped = (char)300;
static unsigned char all_ones = (unsigned char)-1;
static int unsigned_compare = -1 < 0u;
static int plus = +5;
static int promoted_shift = (unsigned char)255 << 4;
static unsigned int wrapped_add = 4294967295u + 2u;

int main(void) {
  if (narrowed_sum != -55) {
    return 1;
  }
  if (wrapped != 44) {
    return 2;
  }
  if (all_ones != 255) {
    return 3;
  }
  if (unsigned_compare != 0) {
    return 4;
  }
  if (plus != 5) {
    return 5;
  }
  if (promoted_shift != 4080) {
    return 6;
  }
  if (wrapped_add != 1u) {
    return 7;
  }
  return 0;
}
