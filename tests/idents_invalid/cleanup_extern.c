
void foo(int* p){ /* Provide the cleanup callback used by this test. */
  // do nothing
}

int main(){ /* Exercise cleanup extern behavior. */
  extern int x __attribute__((cleanup(foo)));
  return 0;
}
