void cleanup_int(int *p);

int main(void) { /* Exercise cleanup missing arg behavior. */
  int value __attribute__((cleanup()));
  return value;
}
