
int main(void){ /* Exercise sizeof behavior. */
  char a = sizeof(int);
  int b = sizeof(a + 10);
  int c = sizeof a;
  return a + b + c;
}
