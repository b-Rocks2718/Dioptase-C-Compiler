// Purpose: Check short* vs int* pointer arithmetic stride differences.
// Expected: main returns 3.
int main(void) {
  int kPointerStep = 1;
  int kExpectedShortStride = 2; // ABI short size per docs/abi.md.
  int kExpectedIntStride = 4; // ABI int size per docs/abi.md.
  int kFlagIntStride = 1;
  int kFlagShortStride = 2;
  int kInitialFlags = 0;

  int i = 0;
  short s = 0;
  int *ip = &i;
  short *sp = &s;

  unsigned int ip_base = (unsigned int)ip;
  unsigned int sp_base = (unsigned int)sp;
  unsigned int ip_next = (unsigned int)(ip + kPointerStep);
  unsigned int sp_next = (unsigned int)(sp + kPointerStep);

  int ip_stride = (int)(ip_next - ip_base);
  int sp_stride = (int)(sp_next - sp_base);

  int flags = kInitialFlags;
  if (ip_stride == kExpectedIntStride) {
    flags += kFlagIntStride;
  }
  if (sp_stride == kExpectedShortStride) {
    flags += kFlagShortStride;
  }
  return flags;
}
