struct S { /* Define the struct used by the struct assign to int test. */
  int a;
};

int main() { /* Exercise struct assign to int behavior. */
  struct S s = {0};
  int x = s;
  return x;
}
