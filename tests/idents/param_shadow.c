int shadow(int x){ /* Return the parameter while exercising name shadowing. */
  int y = x;
  {
    int x = 3;
    y = x;
  }
  return y;
}
