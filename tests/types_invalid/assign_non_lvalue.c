int main(void) { /* Exercise assign non lvalue behavior. */
  int x = 1;
  (x + 1) = 2;
  return x;
}
