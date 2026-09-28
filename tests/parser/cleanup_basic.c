void cleanup_int(int *p);

int main(void) { /* Exercise cleanup basic behavior. */
  __attribute__((cleanup(cleanup_int))) int value = 0;
  int __attribute__((cleanup(cleanup_int))) value2 = 0;
  int value3 __attribute__((cleanup(cleanup_int))) = 0;
  return value;
}
