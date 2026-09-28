/* Regression: nested parentheses around a unary operator with no operand
   must also fail cleanly instead of re-parsing an earlier '('. */
int main(void) {
  return ((~));
}
