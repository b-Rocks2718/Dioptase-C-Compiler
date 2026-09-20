
int main() { /* Exercise struct outside scope behavior. */
  {
    struct Test { /* Define the struct used by the struct outside scope test. */
      int a;
    };
  }
  struct Test t;
  return 0;
}
