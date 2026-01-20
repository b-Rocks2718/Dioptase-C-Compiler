
void* unsafe(void){
  int x;
  return &x;
}

void f(void){
  unsafe();
  return;
}

int main(){
  f();
  return 0;
}
