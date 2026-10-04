/* Backslash-newline splices join physical lines before anything else (C11
   5.1.1.2 phase 2): inside #define bodies, identifiers, strings, and // comments.
   Output keeps one line per source line, so __LINE__ and later lines stay aligned. */
#define SWAP(a, b) do { \
    int tmp = (a);      \
    (a) = (b);          \
    (b) = tmp;          \
  } while (0)
#define TOTAL 1 + \
2
SWAP(x, y);
int spl\
it = TOTAL;
char* s = "ab\
cd";
// this comment continues \
int hidden = 1;
int line = __LINE__;
