// Indirect tail call whose target the register allocator keeps in a
// callee-saved register. f stays live across the call to g, so it is placed in
// r20-r27; the epilogue before the tail jump restores the caller's value of
// that register, so codegen must copy the target out of it first. Jumping
// through the restored register would branch to whatever main left there.
// Expected: main returns 0.

int inc(int x) { return x + 1; }

int twice(int x) { return x * 2; }

// f is live across g(x), then reached by an indirect tail call.
int apply2(int (*f)(int), int (*g)(int), int x) {
  int y = g(x);
  return f(y);
}

int main(void) {
  return apply2(twice, inc, 20) == 42 ? 0 : 1;
}
