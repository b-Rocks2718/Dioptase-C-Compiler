
enum Test { /* Define the struct used by the enum duplicate const2 test. */
  VALUE_ONE,
  VALUE_TWO,
};

enum TestDuplicate { /* Define duplicate enumerators for this invalid test. */
  VALUE_A,
  VALUE_ONE, // Duplicate constant
};
