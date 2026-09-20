void cleanup_int(int *p);

int main(void) { /* Exercise cleanup duplicate location behavior. */
  __attribute__((cleanup(cleanup_int))) int __attribute__((cleanup(cleanup_int))) value;
  return 0;
}
