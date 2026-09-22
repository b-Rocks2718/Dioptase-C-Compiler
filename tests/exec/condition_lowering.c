/*
 * Exercise signedness and short-circuit branches in statement conditions.
 * All seven checks must run without evaluating the skipped increments.
 */
int main(void) { /* Return the accumulated branch outcomes. */
  unsigned int high = 0x80000000u;
  unsigned int low = 1u;
  int result = 0;
  int calls = 0;
  int *ptr = &result;

  if (high > low) result += 1;
  if (high >= low) result += 2;
  if (low < high) result += 4;
  if (low <= high) result += 8;
  if (!(high < low)) result += 16;
  if (!(low > high || high < low) && high >= low) result += 32;
  if (ptr && (high > low || ++calls) && !(low > high && ++calls)) result += 64;

  return result + calls;
}
