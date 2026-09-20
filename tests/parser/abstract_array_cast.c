int (*p)[3];

int main(){ /* Exercise abstract array cast behavior. */
  return ((int (*)[3U])p)[1][2];
}
