// Loads and stores at bp-relative offsets that the absolute-addressing field
// cannot encode (docs/ISA.md: a 12-bit signed immediate scaled by 2^0..2^3,
// so at most +-2047 for odd offsets and +-16376 overall). Codegen used to emit
// these offsets directly, producing assembly the assembler rejected.
// Covered: byte/short/word locals below a 17000-byte array, a char member at
// an odd offset past -2048, storing an address into a far slot, and a stack
// parameter above a 16800-byte by-value struct (positive offset past +16376).
// Each case lives in its own function so no two large frames are live at once;
// the simple emulator's user stack is only 64 KB.
// Expected: main returns 0.

struct Wide {
  char pad[3001];
  char tail;
};

struct Huge {
  int v[4200];
};

// Keeps buf in memory and writes both ends of it.
int touch(char* buf, int i) {
  buf[0] = 1;
  buf[i] = (char)i;
  return buf[0];
}

// Keeps the struct in memory so its members stay bp-relative.
void fill_wide(struct Wide* w) {
  w->pad[0] = 1;
  w->tail = 2;
}

// Reads through a pointer that codegen had to store into a far slot.
int deref(int** pp) {
  return **pp;
}

// Scalars and a pointer slot allocated below the 17000-byte array.
int far_locals(void) {
  char buf[17000];
  volatile char c = 7;
  volatile short s = 1234;
  volatile int w = 56789;
  if (touch(buf, 16999) != 1 || buf[16999] != (char)16999) {
    return 1;
  }
  if (c != 7 || s != 1234 || w != 56789) {
    return 2;
  }
  int target = 42;
  int* p = &target;
  if (deref(&p) != 42) {
    return 3;
  }
  return 0;
}

// A char member at an odd offset more than 2048 bytes below bp.
int far_member(void) {
  struct Wide wide;
  fill_wide(&wide);
  wide.tail = (char)(wide.tail + 3);
  if (wide.tail != 5 || wide.pad[0] != 1) {
    return 4;
  }
  return 0;
}

// x is passed on the stack above the 16800-byte struct.
int after_huge(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
               struct Huge h, int x) {
  return x + h.v[4199] + a1 + a8;
}

// Builds the large by-value argument for after_huge.
int far_stack_arg(void) {
  struct Huge h;
  h.v[4199] = 100;
  if (after_huge(1, 2, 3, 4, 5, 6, 7, 8, h, 9) != 9 + 100 + 1 + 8) {
    return 5;
  }
  return 0;
}

int main(void) {
  int result = far_locals();
  if (result != 0) {
    return result;
  }
  result = far_member();
  if (result != 0) {
    return result;
  }
  return far_stack_arg();
}
