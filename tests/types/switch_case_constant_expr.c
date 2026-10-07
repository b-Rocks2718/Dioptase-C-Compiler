/*
Case labels are integer constant expressions (C11 6.8.4.2p3), not just
literals: they are evaluated during type checking and converted to the
switch's promoted controlling type. The printed case list shows the converted
values in the order cases are dispatched (newest first).
*/
int main(void) {
  char c = 4;
  switch (c) {
    case 1 + 2:
      return 3;
    case (char)260:
      return 4;
    case sizeof(int) * 2:
      return 8;
    default:
      break;
  }
  return 0;
}
