
void foo(int* p){
  // do nothing
}

int main(){
  extern int x __attribute__((cleanup(foo)));
  return 0;
}