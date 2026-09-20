int main(void) { /* Exercise stmt expr empty behavior. */
  int x = ({ });
  return x;
}
