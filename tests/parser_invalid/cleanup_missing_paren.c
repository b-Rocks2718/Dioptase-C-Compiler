void cleanup_int(int *p);

int main(void) { /* Exercise cleanup missing paren behavior. */
  int value __attribute__((cleanup(cleanup_int));
  return value;
}
