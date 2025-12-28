int main(void){
  int i = 0;
  while (i < 3) {
    i = i + 1;
    if (i == 2) break;
  }
  do {
    i = i - 1;
  } while (i > 0);
  for (i = 0; i < 2; i = i + 1) ;
  return i;
}
