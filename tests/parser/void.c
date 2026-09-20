
void* unsafe(void){ /* Return a void pointer to exercise void-type parsing. */
  int x;
  return &x;
}

void f(void){ /* Provide the void function used by the type test. */
  unsafe();
  return;
}

int main(){ /* Exercise void behavior. */
  f();
  return 0;
}
