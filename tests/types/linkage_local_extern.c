int main(void) { /* Exercise linkage local extern behavior. */
  extern int value;
  return value;
}

int value;
