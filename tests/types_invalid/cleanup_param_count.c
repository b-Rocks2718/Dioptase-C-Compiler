void cleanup_two(int *a, int *b);

int main(void) { /* Exercise cleanup param count behavior. */
  int value __attribute__((cleanup(cleanup_two))) = 0;
  return value;
}
