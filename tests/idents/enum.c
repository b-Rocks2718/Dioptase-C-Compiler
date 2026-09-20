
enum Test { /* Define the struct used by the enum test. */
  VALUE_ONE,
  VALUE_TWO = 1,
  VALUE_THREE = 10,
  VALUE_FOUR
};

int main(){ /* Exercise enum behavior. */
  enum Test value = VALUE_TWO;
  if (value == VALUE_ONE) return 1;
  if (value == VALUE_TWO) return 2;
  if (value == VALUE_THREE) return 3;
  if (value == VALUE_FOUR) return 4;
  return 0;
}

void func(enum Test s, enum Test* t){ /* Copy the aggregate arguments to exercise parameter passing. */
  switch (s){
    case VALUE_ONE:
      break;
    case VALUE_TWO:
      break;
    case VALUE_THREE:
      break;
    case VALUE_FOUR:
      break;
  }
}
