/*
 * Inlining a struct-returning function must not copy the fallback
 * `Return Const(0)` (appended to every function for falling off its end) into
 * the struct result. Without dead-code elimination that unreachable copy
 * survives to ASM generation, which cannot copy a scalar constant into a
 * struct. main's inlined body should copy only p into the call result.
 */

struct Pair {
  int a;
  int b;
};

struct Pair make_pair(int a, int b) {
  struct Pair p;
  p.a = a;
  p.b = b;
  return p;
}

int main(void) {
  struct Pair pr = make_pair(4, 6);
  return pr.a * pr.b;
}
