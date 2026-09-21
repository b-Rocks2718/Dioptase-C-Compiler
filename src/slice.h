#ifndef SLICE_H
#define SLICE_H

#include <stdbool.h>
#include <stddef.h>

// View a non-NUL-terminated character span without owning its storage.
struct Slice {
  char const * start; // where does the string start in memory?
  size_t len;        // How many characters in the string
};

// compare a slice to a C string pointer
// true if the slice is equal to the C string pointer, false otherwise
bool compare_slice_to_pointer(const struct Slice* s, char const *p);

// true if the slices are equal, false otherwise
bool compare_slice_to_slice(const struct Slice* self, const struct Slice* other);

struct Slice* slice_concat(const struct Slice* a, const char* b);

bool is_identifier(const struct Slice* slice);

void print_slice(struct Slice* slice);

void print_slice_with_escapes(struct Slice* slice);

size_t hash_slice(const struct Slice* key);

#endif // SLICE_H
