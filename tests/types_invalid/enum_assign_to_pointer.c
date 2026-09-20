enum E { /* Define the enumerators used by the enum assign to pointer test. */
  A = 1
};

int main() { /* Exercise enum assign to pointer behavior. */
  int* p = A;
  return 0;
}
