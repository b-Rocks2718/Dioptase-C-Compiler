union U {
  int a;
};

int main() {
  union U u = {0};
  int x = u;
  return x;
}
