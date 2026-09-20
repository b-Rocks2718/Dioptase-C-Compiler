int main(void) { /* Exercise stmt expr nested behavior. */
  int result = ({ int x = 1; int y = ({ int z = 2; z * 3; }); x + y; });
  return result;
}
