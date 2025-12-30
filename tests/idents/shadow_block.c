int main(void){
  int x = 1;
  {
    int x = 2;
    int y = x;
  }
  return x;
}
