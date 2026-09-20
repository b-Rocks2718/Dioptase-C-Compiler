
struct Test { /* Define the struct used by the struct redecl2 test. */
  int a;
};

int main() { /* Exercise struct redecl2 behavior. */
  union Test t;
  t.a = 5;
  return 0;
}
