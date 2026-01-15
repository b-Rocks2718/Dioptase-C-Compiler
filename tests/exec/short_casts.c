// Purpose: Exercise explicit casts to/from short and unsigned short values.
// Expected: main returns 7.
int main(void) {
  int kShortPosValue = 30000; // Within signed short range per docs/abi.md.
  int kShortNegValue = -12345; // Within signed short range per docs/abi.md.
  unsigned int kUShortValue = 50000; // Within unsigned short range per docs/abi.md.
  int kFlagPos = 1;
  int kFlagNeg = 2;
  int kFlagUnsigned = 4;
  int kInitialFlags = 0;

  int pos_cast = (int)(short)kShortPosValue;
  int neg_cast = (int)(short)kShortNegValue;
  int unsigned_cast = (int)(unsigned short)kUShortValue;

  int flags = kInitialFlags;
  if (pos_cast == kShortPosValue) {
    flags += kFlagPos;
  }
  if (neg_cast == kShortNegValue) {
    flags += kFlagNeg;
  }
  if (unsigned_cast == (int)kUShortValue) {
    flags += kFlagUnsigned;
  }
  return flags;
}
