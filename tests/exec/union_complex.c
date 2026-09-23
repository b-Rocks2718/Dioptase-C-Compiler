// Exercise two integer views sharing the same union storage.
union U {
  int x;
  int y;
};

// Exercise global union initialization.
union U g_union = { 7 };

// Modify a union by updating its x member.
// Returns the updated x value.
int bump_union(union U* u, int delta) {
  u->x = u->x + delta;
  return u->x;
}

// Drive union usage across globals, locals, arrays, and pointers.
int main(void) {
  union U local = { 5 };
  union U arr[2];
  union U* ptr = &arr[1];

  arr[0].x = 3;
  arr[1].y = 4;

  return bump_union(&g_union, 1) +
         bump_union(&local, 2) +
         arr[0].x +
         ptr->y;
}
