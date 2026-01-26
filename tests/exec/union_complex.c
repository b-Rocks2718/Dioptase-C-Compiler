// Purpose: Provide named constants for union/global/local/array coverage.
// Inputs/Outputs: Constants drive initializers and indices.
// Invariants/Assumptions: Values are small signed integers.
#define kArrLen 2
#define kIndex0 0
#define kIndex1 1
#define kInitGlobal 7
#define kInitLocal 5
#define kArrVal0 3
#define kArrVal1 4

// Purpose: Represent a union with two integer views over the same storage.
// Inputs/Outputs: Members x and y alias the same backing bytes.
// Invariants/Assumptions: Reads follow the last-written member in this test.
union U {
  int x;
  int y;
};

// Purpose: Exercise global union initialization.
// Inputs/Outputs: Initializes the first union member with kInitGlobal.
// Invariants/Assumptions: Default union initializer targets the first member.
union U g_union = { kInitGlobal };

// Purpose: Modify a union by updating its x member.
// Inputs: u points to a union; delta is added to x.
// Outputs: Returns the updated x value.
// Invariants/Assumptions: u is non-NULL and x is the active member.
int bump_union(union U* u, int delta) {
  u->x = u->x + delta;
  return u->x;
}

// Purpose: Drive union usage across globals, locals, arrays, and pointers.
// Inputs/Outputs: None.
// Invariants/Assumptions: Array indices stay within [0, kArrLen).
int main(void) {
  union U local = { kInitLocal };
  union U arr[kArrLen];
  union U* ptr = &arr[kIndex1];

  arr[kIndex0].x = kArrVal0;
  arr[kIndex1].y = kArrVal1;

  return bump_union(&g_union, 1) +
         bump_union(&local, 2) +
         arr[kIndex0].x +
         ptr->y;
}
