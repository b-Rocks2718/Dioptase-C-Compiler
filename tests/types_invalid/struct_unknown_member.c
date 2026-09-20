
struct Test { /* Define the struct used by the struct unknown member test. */
  int a;
  short b;
};

int main() { /* Exercise struct unknown member behavior. */
  struct Test t;
  t.c = 5; // Error: 'c' is not a member of 'struct Test'
  return 0;
}
