
enum Test;

enum Test {
  VALUE_ONE,
  VALUE_TWO = 1,
  VALUE_THREE = 10,
  VALUE_FOUR
};

int main(){
  enum Test value = VALUE_TWO;
  if (value == VALUE_ONE) return 1;
  if (value == VALUE_TWO) return 2;
  if (value == VALUE_THREE) return 3;
  if (value == VALUE_FOUR) return 4;
  return 0;
}

void func(enum Test s, enum Test* t){
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
