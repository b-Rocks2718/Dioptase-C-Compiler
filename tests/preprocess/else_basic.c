#define FOO
#ifdef FOO
int a = 1;
#else
int a = 2;
#endif
#ifndef BAR
int b = 3;
#else
int b = 4;
#endif
