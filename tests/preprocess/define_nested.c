/* Test nested object-macro expansion, quoted text, and recursion stops. */ #define BASE 41
#define SIZE BASE
#define ALIAS SIZE
#define SELF SELF
#define FIRST SECOND
#define SECOND FIRST
#define QUOTED "SIZE"
int a[ALIAS];
char *name = QUOTED;
int x = SELF;
int y = FIRST;
