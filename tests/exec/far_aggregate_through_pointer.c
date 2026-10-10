// Struct copies through a pointer where the copied bytes sit at offsets the
// absolute-addressing field cannot encode. Codegen used to keep the pointer in
// scratch r9 for the whole copy; a far frame slot on the other side then used
// r9 as its address temporary, so every later chunk read from a garbage
// address. Covered: loading *p into a far local, storing a far local through
// p, and returning a large struct through the hidden result pointer.
// Expected: main returns 0; a nonzero result identifies the failing case.

struct Big {
  int pad[3000];
  int x;
};

struct Big g;

// Copies *p into a local whose high members sit past the encodable range.
int load_through_pointer(struct Big* p) {
  struct Big local = *p;
  return local.pad[2] * 10 + local.x;
}

// Copies a local into *p, with far offsets on both sides.
void store_through_pointer(struct Big* p) {
  struct Big local;
  local.pad[2500] = 3;
  local.x = 9;
  *p = local;
}

// Returned through the caller's buffer pointer.
struct Big make(int a) {
  struct Big r;
  r.pad[2999] = a;
  r.x = a + 1;
  return r;
}

int main(void) {
  g.pad[2] = 4;
  g.x = 2;
  if (load_through_pointer(&g) != 42) {
    return 1;
  }
  store_through_pointer(&g);
  if (g.pad[2500] != 3 || g.x != 9) {
    return 2;
  }
  struct Big m = make(5);
  if (m.pad[2999] != 5 || m.x != 6) {
    return 3;
  }
  return 0;
}
