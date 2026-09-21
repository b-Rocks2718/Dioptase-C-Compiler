/* Exercise constant folding plus removal of unreachable and unused expressions. */
int main(void){
  if (1 + 1) {
    return 1;
  } else {
    return 0;
  }
  1 + 2;
}
