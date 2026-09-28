void cleanup_ptr(int **p);

int main(void) { /* Exercise cleanup pointer behavior. */
  int value = 0;
  int *ptr __attribute__((cleanup(cleanup_ptr))) = &value;
  return *ptr;
}
