int main(void){ /* Exercise goto into block behavior. */
  goto inside;
  {
inside:
    ;
  }
  return 0;
}
