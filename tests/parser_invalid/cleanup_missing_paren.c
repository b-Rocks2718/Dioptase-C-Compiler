void cleanup_int(int *p);

int main(void) {
  int value __attribute__((cleanup(cleanup_int));
  return value;
}
