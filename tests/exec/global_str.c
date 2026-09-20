char* str = "hello";
char str2[3] = "\tb\n";

int main(void){ /* Exercise global str behavior. */
  return str[0] + str2[0];
}
