
void free_int(int* p) { /* Release the integer supplied to the cleanup callback. */
  return;
}

__attribute__((cleanup(free_int))) int x;
