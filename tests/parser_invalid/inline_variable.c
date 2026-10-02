/*
 * `inline` is a function specifier; C11 6.7.4p1 forbids it on objects.
 */
inline int counter = 0;

int main(void) {
  return counter;
}
