void do_it(void) {
}

int main(void) {
  int x = ({ do_it(); });
  return x;
}
