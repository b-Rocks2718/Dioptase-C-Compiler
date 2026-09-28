int cleanup_target;

int main(void) { /* Exercise cleanup not function behavior. */
  int value __attribute__((cleanup(cleanup_target))) = 0;
  return value;
}
