{
  n = 121;
  max = n;
  while (i != 1){
    if (n % 2 == 0){
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    if (n > max) n = max;
  }
  return max;
}