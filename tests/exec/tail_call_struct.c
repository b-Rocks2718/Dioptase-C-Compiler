/*
Tail calls that return structs. A 16-byte struct is returned in memory, so
the tail-calling function must forward its own return buffer pointer to the
callee (directly and through a function pointer). An 8-byte struct is
returned in r1/r2 and must pass through unchanged. Also tail calls with a
struct argument passed in registers.
Returns the number of checks that produced the expected value.

tac-exec: skip (the TAC interpreter cannot pass or return structs by value)
*/

struct Big {
  int a;
  int b;
  int c;
  int d;
};

struct Pair {
  int x;
  int y;
};

struct Big make_big(int base) { /* Build a struct returned through memory. */
  struct Big big;
  big.a = base;
  big.b = base + 1;
  big.c = base * 2;
  big.d = base * 3;
  return big;
}

struct Big forward_big(int base) { /* Direct tail call returning in memory. */
  return make_big(base + 1);
}

struct Big (*g_make_big)(int);

struct Big forward_big_indirect(int base) { /* Indirect tail call returning in memory. */
  return g_make_big(base + 2);
}

struct Pair make_pair(int v) { /* Build a struct returned in two registers. */
  struct Pair pair;
  pair.x = v;
  pair.y = v * 10;
  return pair;
}

struct Pair forward_pair(int v) { /* Tail call returning in registers. */
  return make_pair(v * 3);
}

int pair_sum(struct Pair pair) { /* Consume a struct argument passed in registers. */
  return pair.x + pair.y;
}

int forward_pair_sum(struct Pair pair) { /* Tail call forwarding a struct argument. */
  return pair_sum(pair);
}

int main(void) { /* Exercise tail calls with struct values. */
  int passed = 0;
  g_make_big = make_big;

  struct Big big = forward_big(4);
  passed += big.a == 5 && big.b == 6 && big.c == 10 && big.d == 15;

  struct Big big2 = forward_big_indirect(4);
  passed += big2.a == 6 && big2.b == 7 && big2.c == 12 && big2.d == 18;

  struct Pair pair = forward_pair(2);
  passed += pair.x == 6 && pair.y == 60;

  passed += forward_pair_sum(pair) == 66;
  return passed;
}
