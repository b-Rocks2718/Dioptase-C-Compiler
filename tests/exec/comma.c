
int main(void){
  short a;
  int b;
  int c = (a = 1, b = 2);
  return a + b + c; // expect 5
}
