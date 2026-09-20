void touch(int *p) { /* Store through the pointer to exercise void statement expressions. */
  *p += 3;
}

int main(void) { /* Exercise stmt expr void tail expr behavior. */
  int x = 4;
  ({
    int y = 1;
    x += y;
    touch(&x);
  });
  return x;
}
