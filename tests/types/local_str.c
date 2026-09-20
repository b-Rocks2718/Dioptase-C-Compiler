int main(void){ /* Exercise local str behavior. */
  char* str = "hello";
  char str2[3] = "\tb\n";
  return str[0] + str2[0];
}
