// Copies of global aggregates larger than 2 KB. Codegen splits these into
// member moves whose addresses are label + offset; offsets past 2047 do not
// fit the 12-bit add immediate (docs/ISA.md) and used to be emitted directly,
// producing assembly the assembler rejected. Covers assignment, pass by value,
// and return by value, plus a char at an odd offset past 2048.
// Expected: main returns 0.

struct Big {
  int v[1000];
  char odd[3];
  int last;
};

struct Big src;
struct Big dst;

// Reads members near the end of a by-value copy.
int sum_tail(struct Big b) {
  return b.v[999] + b.odd[2] + b.last;
}

// Returns the global by value through the caller's buffer.
struct Big get_src(void) {
  return src;
}

int main(void) {
  src.v[0] = 1;
  src.v[999] = 2;
  src.odd[2] = 3;
  src.last = 4;

  dst = src;
  if (dst.v[0] != 1 || dst.v[999] != 2 || dst.odd[2] != 3 || dst.last != 4) {
    return 1;
  }
  if (sum_tail(src) != 2 + 3 + 4) {
    return 2;
  }
  struct Big copy = get_src();
  if (copy.v[999] != 2 || copy.last != 4) {
    return 3;
  }
  return 0;
}
