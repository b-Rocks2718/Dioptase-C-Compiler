int main(void){
  int x = 1;
  int *p = &x;
  p = &x;
  x = (int) x;
  return *p;
}
