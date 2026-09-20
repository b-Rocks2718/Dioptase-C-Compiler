enum Color { /* Define the enumerators used by the enum basic test. */
  RED = 1,
  GREEN = 4,
  BLUE = 10
};

int main(void) { /* Exercise enum basic behavior. */
  enum Color c = GREEN;
  return c + BLUE;
}
