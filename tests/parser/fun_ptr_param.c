int inc(int x){
  return x + 1;
}

int apply(int (*fn)(int), int x){
  return (*fn)(x);
}
