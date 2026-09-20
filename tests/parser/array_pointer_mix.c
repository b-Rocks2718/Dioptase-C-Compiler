int (*ptr)[3];
int *rows[2];

int main(){ /* Exercise array pointer mix behavior. */
  return (*ptr)[1] + rows[0][2];
}
