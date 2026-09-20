enum E { /* Define the enumerators used by the enum compare other test. */
  A = 1
};

int main() { /* Exercise enum compare other behavior. */
  enum E e = A;
  int* p = e;
  return 0;
}
