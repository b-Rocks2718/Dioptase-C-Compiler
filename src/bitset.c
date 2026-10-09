#include "bitset.h"
#include "checked_alloc.h"

#include <stdlib.h>
#include <string.h>

size_t bitset_word_count(size_t bit_count) {
  size_t word_count = (bit_count + BITSET_WORD_BITS - 1) / BITSET_WORD_BITS;
  return word_count == 0 ? 1 : word_count;
}

struct Bitset bitset_alloc(size_t word_count, const char* component, const char* purpose) {
  struct Bitset set;
  set.word_count = word_count;
  set.words = checked_calloc(word_count, sizeof(*set.words), component, purpose);
  return set;
}

void bitset_free(struct Bitset* set) {
  free(set->words);
  set->words = NULL;
  set->word_count = 0;
}

struct Bitset bitset_view(uint64_t* words, size_t word_count, size_t index) {
  struct Bitset set;
  set.words = words + index * word_count;
  set.word_count = word_count;
  return set;
}

void bitset_clear(struct Bitset set) {
  memset(set.words, 0, set.word_count * sizeof(*set.words));
}

void bitset_copy(struct Bitset dst, struct Bitset src) {
  memcpy(dst.words, src.words, dst.word_count * sizeof(*dst.words));
}

void bitset_union(struct Bitset dst, struct Bitset src) {
  for (size_t i = 0; i < dst.word_count; ++i) {
    dst.words[i] |= src.words[i];
  }
}

void bitset_intersect(struct Bitset dst, struct Bitset src) {
  for (size_t i = 0; i < dst.word_count; ++i) {
    dst.words[i] &= src.words[i];
  }
}

void bitset_subtract(struct Bitset dst, struct Bitset src) {
  for (size_t i = 0; i < dst.word_count; ++i) {
    dst.words[i] &= ~src.words[i];
  }
}

bool bitset_equal(struct Bitset a, struct Bitset b) {
  return memcmp(a.words, b.words, a.word_count * sizeof(*a.words)) == 0;
}

bool bitset_test(struct Bitset set, size_t bit) {
  return (set.words[bit / BITSET_WORD_BITS] & (UINT64_C(1) << (bit % BITSET_WORD_BITS))) != 0;
}

void bitset_set(struct Bitset set, size_t bit) {
  set.words[bit / BITSET_WORD_BITS] |= UINT64_C(1) << (bit % BITSET_WORD_BITS);
}

void bitset_reset(struct Bitset set, size_t bit) {
  set.words[bit / BITSET_WORD_BITS] &= ~(UINT64_C(1) << (bit % BITSET_WORD_BITS));
}
