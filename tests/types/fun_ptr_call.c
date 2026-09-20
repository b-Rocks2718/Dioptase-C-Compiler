int add_one(int x) { /* Add one. */
  return x + 1;
}

int main(void) { /* Exercise fun ptr call behavior. */
  int (*fp)(int) = add_one;
  int y = fp(41);
  return y;
}
