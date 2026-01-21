#define TEST_ZERO 0

void cleanup_int_ptr(int **p);
void cleanup_arr_ptrs(int *(*p)[2]);
void cleanup_arr(int (*p)[2]);
void cleanup_fp(int (**p)(int));

int main(void) {
  __attribute__((cleanup(cleanup_int_ptr))) int **pp;
  int __attribute__((cleanup(cleanup_arr_ptrs))) *arr[2];
  int (*arr_ptr)[2] __attribute__((cleanup(cleanup_arr)));
  int (*fp)(int) __attribute__((cleanup(cleanup_fp)));
  return TEST_ZERO;
}
