int shadow(int x){
  int y = x;
  {
    int x = 3;
    y = x;
  }
  return y;
}
