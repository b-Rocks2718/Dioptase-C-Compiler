int main(void) { /* Exercise unsigned long promo behavior. */
  unsigned int u = 1;
  unsigned long ul = 2;
  unsigned long sum = u + ul;
  return sum;
}
