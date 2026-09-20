void touch(int *p) { /* Store through the pointer to exercise void statement expressions. */
  *p = 1;
}

int main(void) { /* Exercise stmt expr void behavior. */
  int x = 0;
  ({
    int y = 2;
    if (y) {
      touch(&x);
    }
  });
  return x;
}
