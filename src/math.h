#ifndef MATH_H
#define MATH_H

#include <stdbool.h>
#include <stddef.h>

// Return the minimum of two integers.
int int_min(int a, int b);

// Return the maximum of two integers.
int int_max(int a, int b);

// Return true if x is a power of two (1, 2, 4, ...); false for 0.
bool is_power_of_two(size_t x);

// Return floor(log2(x)), which is exact when x is a power of two. x must be
// nonzero; log2_size(0) returns 0.
unsigned log2_size(size_t x);

#endif // MATH_H
