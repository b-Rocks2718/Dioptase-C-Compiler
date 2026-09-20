union U { /* Define the union used by the union assign to int test. */
  int a;
};

int main() { /* Exercise union assign to int behavior. */
  union U u = {0};
  int x = u;
  return x;
}
