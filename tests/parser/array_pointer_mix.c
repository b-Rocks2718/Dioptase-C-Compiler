int (*ptr)[3];
int *rows[2];

int main(){
  return (*ptr)[1] + rows[0][2];
}
