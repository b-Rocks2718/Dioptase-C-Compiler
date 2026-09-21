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

// One node in an insertion-ordered list of slices.
struct SliceListNode {
  struct Slice* slice;
  struct SliceListNode* next;
};

// Linked list of slices with O(1) append via last.
// Empty lists have head == last == NULL. Nodes store slice pointers
// and do not copy slice contents.
struct SliceList {
  struct SliceListNode* head;
  struct SliceListNode* last;
};

// Append slice onto list. A NULL slice is ignored.
void slice_list_add(struct SliceList* list, struct Slice* slice);

// Return true if list contains a slice equal to slice.
bool slice_list_contains(struct SliceList list, struct Slice* slice);

// Copy list nodes, sharing the original slice pointers.
struct SliceList copy_slice_list(struct SliceList src);

#endif // SLICE_H
