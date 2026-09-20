static int g = 1;
extern int h;
int main(void){ /* Exercise storage classes behavior. */
  return g + h;
}
