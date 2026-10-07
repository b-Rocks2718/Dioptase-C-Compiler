/* #undef removes object-like and function-like macros; undefining an unknown
   name is allowed, and a name may be redefined after #undef. */
#define VALUE 1
#define F(x) (x)
int a = VALUE + F(2);
#undef VALUE
#undef F
#undef NEVER_DEFINED
#ifdef VALUE
int wrong = 1;
#endif
int b = VALUE + F(2);
#define VALUE 3
int c = VALUE;
