int main(void) { /* Exercise stmt expr no expr end behavior. */
  int x = ({ int y = 0; });
  return x;
}
