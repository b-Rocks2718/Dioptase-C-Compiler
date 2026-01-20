
int main(void){
  char a = sizeof(int);
  int b = sizeof(a + 10);
  int c = sizeof a;
  return a + b + c;
}
