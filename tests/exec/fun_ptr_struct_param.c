/*
Struct tags inside function types must resolve to the same tag as everywhere
else. Function types nested under a pointer or a cast, repeated prototypes, and
block-scope function declarations used to leave `struct T` unresolved in their
parameter lists, so their types did not match `inc`'s and calls through them
were rejected as incompatible.
*/
struct T {
  int x;
};

void inc(struct T* t);
void inc(struct T* t);

void inc(struct T* t) {
  t->x++;
}

static void apply(void (*cb)(struct T*), struct T* t) {
  cb(t);
}

int main(void) {
  struct T t = {0};

  void (*fn)(struct T*) = inc;
  fn(&t);

  apply(inc, &t);

  void* erased = (void*)inc;
  ((void (*)(struct T*))erased)(&t);

  void inc(struct T* t);
  inc(&t);

  return t.x == 4 ? 0 : t.x + 1;
}
