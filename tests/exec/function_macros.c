/* Exercise function-like macros end to end: argument substitution with side
   effects, nesting, ## pasting into identifiers, # stringizing, variadic
   arguments, an invocation spanning lines, a backslash-continued definition,
   and #undef. The host compiler's result is the expected value. */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SQ(x) ((x) * (x))
#define FIELD(n) field_ ## n
#define STR(x) #x
#define SUM3(...) sum3(__VA_ARGS__)
#define APPLY(f, x) f(x)
#define SWAP(a, b) do { \
    int tmp = (a);      \
    (a) = (b);          \
    (b) = tmp;          \
  } while (0)
#define SCALE 100
#undef SCALE
#define SCALE 10

int sum3(int a, int b, int c) {
  return a + b + c;
}

int main(void) {
  int field_1 = 4;
  int field_2 = 9;
  int count = 0;
  int m = MAX(count++, -1); /* count++ evaluated twice: m == 1, count == 2 */
  char* s = STR(a + b);     /* "a + b" */
  int total = SUM3(FIELD(1),
                   FIELD(2),
                   APPLY(SQ, 3));
  SWAP(field_1, field_2);   /* field_1 == 9, field_2 == 4 */
  return MAX(SQ(2), 3) + m + count + s[2] + total /* 4 + 1 + 2 + '+' + 22 */
         + field_1 - field_2 + SCALE;             /* + 5 + 10 */
}
