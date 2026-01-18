int (*p)[3];

int main(){
  return ((int (*)[3U])p)[1][2];
}
