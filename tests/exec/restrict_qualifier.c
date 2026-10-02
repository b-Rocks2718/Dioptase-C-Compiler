/*
 * `restrict` is accepted wherever C allows it on a pointer (parameters,
 * locals, multi-level pointers, casts, sizeof) and has no effect on codegen.
 */

static int sum_into(int* restrict dst, const int* restrict src, int n) {
  for (int i = 0; i < n; i++) {
    *dst += src[i];
  }
  return *dst;
}

static int deref2(int* restrict* restrict pp) {
  return **pp;
}

int main(void) {
  int values[3] = {1, 2, 3};
  int acc = 10;
  int* restrict acc_ptr = &acc;
  int* const restrict values_ptr = values;

  int total = sum_into(acc_ptr, values_ptr, 3);

  total += deref2(&acc_ptr);
  total += *(int* restrict)values;
  total += sizeof(int* restrict) == sizeof(int*);
  return total;
}
