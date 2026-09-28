/* Regression: a unary operator with no operand inside parentheses, "(-)3",
   used to rewind the parser past the '(' and recurse until the stack
   overflowed. It must be reported as an ordinary parse error. */
int main(void) {
  return (-)3;
}
