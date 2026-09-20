int main(void){ /* Exercise shadow block behavior. */
  int x = 1;
  {
    int x = 2;
    int y = x;
  }
  return x;
}
