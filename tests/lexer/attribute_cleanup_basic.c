void cleanup_int(int *p);

int main(void) { /* Exercise attribute cleanup basic behavior. */
  int value __attribute__((cleanup(cleanup_int)));
  return value;
}
