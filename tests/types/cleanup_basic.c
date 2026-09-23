void cleanup_int(int *p);

int main(void) { /* Exercise cleanup basic behavior. */
  int value __attribute__((cleanup(cleanup_int))) = 0;
  return value;
}
