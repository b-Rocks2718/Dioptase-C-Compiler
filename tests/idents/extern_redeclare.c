int g;

int main(void){ /* Exercise extern redeclare behavior. */
  extern int g;
  g = 1;
  return g;
}
