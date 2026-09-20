int main(void){ /* Exercise switch in loop behavior. */
  while (1) {
    switch (1) {
      case 0:
        break;
      default:
        continue;
    }
    break;
  }
  return 0;
}
