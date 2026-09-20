int main(void){ /* Exercise dangling else behavior. */
  if (1) else return 0;
}
