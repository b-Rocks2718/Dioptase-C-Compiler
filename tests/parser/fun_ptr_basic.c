int add(int a, int b){
  return a + b;
}

int main(void){
  int (*fp)(int, int) = add;
  return fp(3, 4);
}
