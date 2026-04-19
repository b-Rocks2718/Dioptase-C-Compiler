int main(void){
  char esc = '\x1b';
  char* color = "\x1b[31m";

  if (esc != 27) return 1;
  if (color[0] != 27) return 2;
  if (color[1] != '[') return 3;
  if (color[2] != '3') return 4;
  if (color[3] != '1') return 5;
  if (color[4] != 'm') return 6;
  if (color[5] != '\0') return 7;
  return 0;
}
