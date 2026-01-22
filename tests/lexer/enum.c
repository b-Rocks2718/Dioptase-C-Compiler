
enum Test {
  VALUE_ONE,
  VALUE_TWO,
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
