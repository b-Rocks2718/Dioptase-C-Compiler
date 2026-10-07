#ifndef SLICE_INDEX_H
#define SLICE_INDEX_H

#include "slice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Dense numbering of Slice names: each distinct name gets the next id (0, 1,
// 2, ...) on first insertion. Used by optimizer passes that keep per-name
// bitsets or arrays. Open addressing with linear probing over a power-of-two
// table that doubles to keep the load factor at most 1/2. Heap-allocated;
// names are borrowed and must outlive the index.

#define SLICE_INDEX_NONE UINT32_MAX

struct SliceIndex {
  const struct Slice** slot_names; // NULL marks an empty slot
  uint32_t* slot_ids;
  size_t slot_count;
  uint32_t count;
};

// Initialize an empty index sized for about expected_names names.
void slice_index_init(struct SliceIndex* index, size_t expected_names);

void slice_index_free(struct SliceIndex* index);

// Id of name, adding it with the next id if absent. *added (may be NULL)
// reports whether the name was new. name must not be NULL.
uint32_t slice_index_add(struct SliceIndex* index, const struct Slice* name, bool* added);

// Id of name, or SLICE_INDEX_NONE when absent.
uint32_t slice_index_get(const struct SliceIndex* index, const struct Slice* name);

#endif // SLICE_INDEX_H
