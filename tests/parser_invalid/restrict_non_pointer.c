/*
 * `restrict` may only qualify pointer types (C11 6.7.3p2). Here it lands on
 * int, not on the pointer, because it precedes the '*'.
 */
int main(void) {
  int value = 0;
  int restrict* p = &value;
  return *p;
}
