// scratchpad for random tests and experiments

/*
int next_collatz(int x) {
  if (x % 2 == 0)
    return x / 2;
  else
    return 3 * x + 1;
}*/

struct Test {
  int u;
  int v;
  int w;
};

int main() {
  static struct Test global_struct;
  static int global_scalar = 42;
  struct Test local_struct;
  int local_scalar;

  int local_arr[3];

  local_arr[2] = local_scalar;

  int* p = &global_struct.v;
  global_struct.v = global_scalar;
  global_struct.w + global_scalar;

  int* q = &local_struct.v;
  local_struct.v = local_scalar;
  local_struct.w + local_scalar;
  
  return 0;
}
