int add(int a, int b) { /* Add the arguments for this arithmetic test. */
  return a + b;
}

int main(void) { /* Exercise fun ptr indirect call behavior. */
  int (*fp)(int, int) = add;
  return (*fp)(3, 4);
}
