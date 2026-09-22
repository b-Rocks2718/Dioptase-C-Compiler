/*
 * Dead-code elimination removes blocks that cannot be reached and drops
 * jumps and labels that only fall through. A reachable loop is kept, along
 * with the jump that forms its back edge.
 */

int main(void) {
  goto done;
  return 1;
done:
  return 2;
}

int reachable_loop(void) {
loop:
  goto loop;
  return 3;
}
