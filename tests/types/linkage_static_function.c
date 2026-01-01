int shared_value;

static int helper(void) {
  return shared_value;
}

int main(void) {
  return helper();
}
