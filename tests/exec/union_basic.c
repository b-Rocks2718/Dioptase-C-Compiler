union U { /* Define the union used by the union basic test. */
  int a;
  int b;
};

int main(void) { /* Exercise union basic behavior. */
  union U u = {5};
  u.a = u.a + 2;
  return u.a;
}
