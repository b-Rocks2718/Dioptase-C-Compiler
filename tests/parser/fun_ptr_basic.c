int add(int a, int b){ /* Add the arguments for this arithmetic test. */
  return a + b;
}

int main(void){ /* Exercise fun ptr basic behavior. */
  int (*fp)(int, int) = add;
  return fp(3, 4);
}
