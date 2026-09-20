
struct Test { /* Define the struct used by the struct shadow test. */
  int a;
};

int main() { /* Exercise struct shadow behavior. */
  {
    struct Test { /* Define the struct used by the struct shadow test. */
      short b;
    };
    struct Test t2;
    t2.b = 3;
  }

  struct Test t;
  t.a = 5;
  return 0;
}
