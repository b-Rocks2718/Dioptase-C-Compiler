
int x;

void inc(){ /* Return the incremented value used by this test. */
  x++;
  return;
}

int main(){ /* Exercise void behavior. */
  inc();
  inc();
  inc();
  return x;
}
