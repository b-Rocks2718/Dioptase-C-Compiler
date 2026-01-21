
void foo(int* p){
  // do nothing
}

int main(){
  static int x __attribute__((cleanup(foo)));
  return 0;
}