// scratchpad for random tests and experiments

int sub(int x, int y);

int add(int x, int y) {
  return x + y;
}

int many_instrs(int x, int y) {
  for (int i = 0; i < 100; i++) {
    x = sub(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  for (int i = 0; i < 100; i++) {
    x = add(x, y);
  }
  return x;
}

int main() {
  int result = add(3, 4);
  return result;
}
