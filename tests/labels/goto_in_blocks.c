int main(void){
start:
  if (1) {
    goto done;
  }
  while (0) {
    goto start;
  }
done:
  return 0;
}
