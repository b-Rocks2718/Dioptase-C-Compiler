/* Function-like macros: arguments with nested commas, quotes, and line breaks;
   names not followed by '(' are plain identifiers; nested invocations. */
#define SQ(x) ((x) * (x))
#define ID(x) x
#define CALL ID
#define ZERO() 0
#define PAIR(a, b) a + b
int SQ = 3;
int d = SQ (d + 1);
int e = SQ(
  d +
  e);
int f = CALL(5) + ID(ID(6)) + ZERO();
int g = ID(__LINE__) + __LINE__;
int h = PAIR(ID((1, 2)), "a, b(") + ID(')');
int i = SQ(SQ(2));
int k = ID
