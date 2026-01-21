int main(void) {
  int x = ({ int y = 0; });
  return x;
}
