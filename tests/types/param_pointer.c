int deref(int *p) { /* Read through the pointer parameter. */
  return *p;
}

int main(void) { /* Exercise param pointer behavior. */
  int x = 7;
  return deref(&x);
}
