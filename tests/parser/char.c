static char x = 1;
static char esc = '\x1b';

int main(void){
  int a = 1;
  signed char b = a;
  unsigned char c = '\\';
  char d = 'A';
  char e = '\x1b';
  return d + esc + e;
}
