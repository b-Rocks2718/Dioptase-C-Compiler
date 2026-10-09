// Member and constant-index accesses through a pointer carry their byte
// offset in the Load/Store instead of a separate pointer add. Check that
// offsets accumulate across nested members (p->in.b, (*p).in.b, q[2].x),
// that &p->field still yields the adjusted address, that whole-struct copies
// through a pointer start at the member's offset, and that read-modify-write
// forms (+=, ++) reuse the same folded location.
// Expected: main returns 0.

struct In {
  int a;
  int b;
};

struct S {
  int x;
  struct In in;
  char tag;
};

// Every check goes through a pointer parameter so the accesses cannot be
// rewritten as direct named-object offsets.
int check(struct S* p, struct S* q, int* arr) {
  p->x = 1;
  p->in.a = 2;
  p->in.b = 3;
  p->tag = 4;
  if (p->x != 1 || p->in.a != 2 || p->in.b != 3 || (*p).in.b != 3 || p->tag != 4) {
    return 1;
  }

  q[2].x = 10;
  q[2].in.b = 11;
  if (q[2].x != 10 || q[2].in.b != 11 || (q + 2)->in.b != 11) {
    return 2;
  }

  int* pb = &p->in.b;
  struct In* pin = &q[2].in;
  if (*pb != 3 || pin->b != 11) {
    return 3;
  }

  struct In copy = p->in;
  if (copy.a != 2 || copy.b != 3) {
    return 4;
  }
  struct In fresh;
  fresh.a = 20;
  fresh.b = 21;
  q[1].in = fresh;
  if (q[1].in.a != 20 || q[1].in.b != 21) {
    return 5;
  }

  arr[3] = 7;
  arr[3] += 5;
  arr[3]++;
  p->in.b += 4;
  p->in.b++;
  if (arr[3] != 13 || p->in.b != 8 || arr[0] != 0) {
    return 6;
  }
  return 0;
}

int main(void) {
  struct S one;
  struct S many[3];
  int arr[4];
  arr[0] = 0;
  return check(&one, many, arr);
}
