int main(void){ /* Exercise goto undefined behavior. */
  goto nowhere;
  return 0;
}
