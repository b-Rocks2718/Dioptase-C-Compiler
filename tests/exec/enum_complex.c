// Provide an enum with explicit, non-sequential values.
// Mode values remain stable across compilation units.
enum Mode {
  MODE_IDLE = 1,
  MODE_RUN = 4,
  MODE_SLEEP = 7
};

#define kModeCount 3

// Map a mode to a numeric score using a switch.
// Returns the score for the mode, or 0 for default.
int score(enum Mode m) {
  switch (m) {
    case MODE_IDLE:
      return 10;
    case MODE_RUN:
      return 20;
    case MODE_SLEEP:
      return 30;
    default:
      return 0;
  }
}

// Exercise enum arrays, switch dispatch, and arithmetic with enums.
int main(void) {
  enum Mode modes[kModeCount] = { MODE_IDLE, MODE_SLEEP, MODE_RUN };
  int i = 0;
  int total = 0;

  for (i = 0; i < kModeCount; i = i + 1) {
    total = total + score(modes[i]);
  }

  return total + MODE_RUN + 2;
}
