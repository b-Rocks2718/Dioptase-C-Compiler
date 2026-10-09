#ifndef BITSET_H
#define BITSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Fixed-width bitsets for dataflow analyses. Passes number their variables
// densely (usually with a SliceIndex) and use the ids as bit positions. Every
// set taking part in one operation must have the same word_count; operations
// do not check this.
//
// A Bitset is a view: it either owns heap words from bitset_alloc (release
// with bitset_free) or points into a caller-owned array of per-block sets
// (bitset_view), which must not be freed through the view.

// Word width, fixed so results do not depend on the host's integer sizes.
#define BITSET_WORD_BITS 64

struct Bitset {
  uint64_t* words;
  size_t word_count;
};

// Words needed for bit_count bits. Always at least 1, so allocations are never
// zero-sized (calloc may return NULL for those).
size_t bitset_word_count(size_t bit_count);

// Allocate an owning, empty set. Allocation failure prints "<component>
// error: ... while <purpose>" and exits with BCC_EXIT_INTERNAL.
struct Bitset bitset_alloc(size_t word_count, const char* component, const char* purpose);

// Release an owning set's words and leave it empty. Never call on a view.
void bitset_free(struct Bitset* set);

// Non-owning view of the index-th set in a contiguous array of sets that are
// word_count words each.
struct Bitset bitset_view(uint64_t* words, size_t word_count, size_t index);

void bitset_clear(struct Bitset set);

// Replace dst's contents with src's.
void bitset_copy(struct Bitset dst, struct Bitset src);

// dst |= src.
void bitset_union(struct Bitset dst, struct Bitset src);

// dst &= src.
void bitset_intersect(struct Bitset dst, struct Bitset src);

// dst &= ~src: remove every member of src from dst.
void bitset_subtract(struct Bitset dst, struct Bitset src);

bool bitset_equal(struct Bitset a, struct Bitset b);

// Bit operations; bit must be below word_count * BITSET_WORD_BITS.
bool bitset_test(struct Bitset set, size_t bit);
void bitset_set(struct Bitset set, size_t bit);
void bitset_reset(struct Bitset set, size_t bit);

#endif // BITSET_H
