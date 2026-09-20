
void foo(int* p){ /* Provide the cleanup callback used by this test. */
  // do nothing
}

int main(){ /* Exercise cleanup static behavior. */
  static int x __attribute__((cleanup(foo)));
  return 0;
}
