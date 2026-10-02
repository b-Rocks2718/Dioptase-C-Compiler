/*
 * `inline` is a function specifier and cannot appear on a struct member.
 */
struct Point {
  inline int x;
};

int main(void) {
  return 0;
}
