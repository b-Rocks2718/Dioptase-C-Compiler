int add1(int x) { /* Add one to the argument for the function-pointer test. */
  return x + 1;
}

int sub1(int x) { /* Subtract one from the argument for the function-pointer test. */
  return x - 1;
}

int (*choose(int use_add))(int) { /* Select the callback exercised by this test. */
  if (use_add) {
    return add1;
  }
  return sub1;
}

int main(void) { /* Exercise fun ptr return behavior. */
  int (*fn)(int) = choose(0);
  return fn(10);
}
