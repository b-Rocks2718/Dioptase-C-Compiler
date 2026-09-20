int (*grid)[2][3];

int main(){ /* Exercise array pointer nested behavior. */
  int local[2][3];
  return (*grid)[1][2] + local[0][1];
}
