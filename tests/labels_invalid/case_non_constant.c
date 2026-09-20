int main(void){ /* Exercise case non constant behavior. */
  switch (1) {
    case 1 + 2:
      break;
  }
  return 0;
}
