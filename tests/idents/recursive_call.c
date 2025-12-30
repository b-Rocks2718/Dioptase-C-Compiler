int ping(int n){
  if (n) return ping(n - 1);
  return 0;
}
