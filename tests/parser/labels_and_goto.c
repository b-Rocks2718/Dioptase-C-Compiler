int main(void){ /* Exercise labels and goto behavior. */
  int x = 0;
start:
  x = x + 1;
  if (x < 3) goto start;
  return x;
}
