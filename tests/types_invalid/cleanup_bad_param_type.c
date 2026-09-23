void cleanup_short(short *p);

int main(void) { /* Exercise cleanup bad param type behavior. */
  int value __attribute__((cleanup(cleanup_short))) = 0;
  return value;
}
