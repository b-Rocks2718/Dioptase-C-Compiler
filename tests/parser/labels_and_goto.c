int main(void){
  int x = 0;
start:
  x = x + 1;
  if (x < 3) goto start;
  return x;
}
