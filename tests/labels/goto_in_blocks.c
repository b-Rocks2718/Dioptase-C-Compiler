int main(void){ /* Exercise goto in blocks behavior. */
start:
  if (1) {
    goto done;
  }
  while (0) {
    goto start;
  }
done:
  return 0;
}
