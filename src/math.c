#include "math.h"

// Return the minimum of two integers.
int int_min(int a, int b) {
  return (a < b) ? a : b;
}

// Return the maximum of two integers.
int int_max(int a, int b) {
  return (a > b) ? a : b;
}

// Return true if x is a power of two (1, 2, 4, ...); false for 0.
bool is_power_of_two(size_t x) {
  return x != 0 && (x & (x - 1)) == 0;
}

// Return floor(log2(x)); log2_size(0) returns 0.
unsigned log2_size(size_t x) {
  unsigned result = 0;
  while (x > 1) {
    x >>= 1;
    result++;
  }
  return result;
}

