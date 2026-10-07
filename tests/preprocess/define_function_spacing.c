/* Expansion must not merge adjacent tokens into a different token, and the
   GNU ", ## __VA_ARGS__" extension deletes the comma for empty arguments. */
#define NEG(x) -x
#define MINUS -
#define EMPTY
#define LOG(fmt, ...) printf(fmt, ## __VA_ARGS__)
int a = -NEG(-1);
int b = MINUS-1;
int c = -EMPTY-1;
LOG("a");
LOG("b %d %d", 1, 2);
