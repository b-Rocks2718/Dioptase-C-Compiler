// Provide named constants for union/global/local/array coverage.
#define kArrLen 2
#define kIndex0 0
#define kIndex1 1
#define kInitGlobal 7
#define kInitLocal 5
#define kArrVal0 3
#define kArrVal1 4

// Exercise two integer views sharing the same union storage.
union U {
  int x;
  int y;
};

// Exercise global union initialization.
union U g_union = { kInitGlobal };

// Modify a union by updating its x member.
// Returns the updated x value.
int bump_union(union U* u, int delta) {
  u->x = u->x + delta;
  return u->x;
}

// Drive union usage across globals, locals, arrays, and pointers.
// Array indices stay within [0, kArrLen).
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
