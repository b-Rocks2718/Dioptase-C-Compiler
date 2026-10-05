/*
Case labels may be any integer constant expression. Each value is converted
to the switch's promoted controlling type before matching, so (char)260 is
case 4, and an unsigned switch matches -1 as UINT_MAX. Each check returns a
distinct code on failure.
*/
enum Constants { kBase = 10 };

static int classify(int x) {
  switch (x) {
    case kBase + 1:
      return 1;
    case 3 * 4:
      return 2;
    case (1 << 4) - 1:
      return 3;
    case sizeof(int) * 5:
      return 4;
    default:
      return 0;
  }
}

static int narrow(char c) {
  switch (c) {
    case (char)260:
      return 7;
    default:
      return 0;
  }
}

static int unsigned_switch(unsigned int u) {
  switch (u) {
    case -1:
      return 9;
    default:
      return 0;
  }
}

int main(void) {
  if (classify(11) != 1) {
    return 1;
  }
  if (classify(12) != 2) {
    return 2;
  }
  if (classify(15) != 3) {
    return 3;
  }
  if (classify(20) != 4) {
    return 4;
  }
  if (narrow(4) != 7) {
    return 5;
  }
  if (unsigned_switch(4294967295u) != 9) {
    return 6;
  }
  return 0;
}
