
void free_int(int* p) {
  return;
}

__attribute__((cleanup(free_int))) int x;