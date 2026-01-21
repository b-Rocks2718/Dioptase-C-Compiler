
int main(void){
  int result = ({ int x = 5; 
    for (int i = 0; ; i++) { 
      x += i; 
      if (i >= 10) break;
    }
    x + 10; 
  });
  return result;
}
