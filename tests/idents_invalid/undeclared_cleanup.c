int main(void) { /* Exercise undeclared cleanup behavior. */
  __attribute__((cleanup(cleanup_int))) int value = 0;
  return value;
}
