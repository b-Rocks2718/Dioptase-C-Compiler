int main(void) { /* Exercise stmt expr condition behavior. */
  int x = 0;
  if (({ int y = 2; x = y; x; })) {
    x += 5;
  }
  return x;
}
