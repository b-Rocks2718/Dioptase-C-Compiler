int main(void){ /* Exercise out of scope var behavior. */
  {
    int x = 1;
  }
  return x;
}
