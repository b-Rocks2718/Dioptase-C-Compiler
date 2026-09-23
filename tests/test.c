// scratchpad for random tests and experiments

void fun() {

}

int main() {
  // call fun through pointer
  void (*p)() = fun;
  p();
  return 0;
}
