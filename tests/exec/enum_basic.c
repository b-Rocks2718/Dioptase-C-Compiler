enum Color {
  RED = 1,
  GREEN = 4,
  BLUE = 10
};

int main(void) {
  enum Color c = GREEN;
  return c + BLUE;
}
