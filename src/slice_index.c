#include "slice_index.h"
#include "checked_alloc.h"

#include <stdio.h>
#include <stdlib.h>


// Allocate an empty table of slot_count (a power of two) slots.
static void slice_index_alloc_slots(struct SliceIndex* index, size_t slot_count) {
  index->slot_count = slot_count;
  index->slot_names = checked_calloc(slot_count, sizeof(*index->slot_names), "Slice index", "growing the table");
  index->slot_ids = checked_calloc(slot_count, sizeof(*index->slot_ids), "Slice index", "growing the table");
}

void slice_index_init(struct SliceIndex* index, size_t expected_names) {
  size_t slots = 16;
  while (slots < expected_names * 2) {
    slots *= 2;
  }
  index->count = 0;
  slice_index_alloc_slots(index, slots);
}

void slice_index_free(struct SliceIndex* index) {
  free(index->slot_names);
  free(index->slot_ids);
  index->slot_names = NULL;
  index->slot_ids = NULL;
  index->slot_count = 0;
  index->count = 0;
}

// Slot holding name, or the empty slot where it would be inserted.
static size_t slice_index_probe(const struct SliceIndex* index, const struct Slice* name) {
  size_t mask = index->slot_count - 1;
  size_t slot = hash_slice(name) & mask;
  while (index->slot_names[slot] != NULL &&
         !compare_slice_to_slice((struct Slice*)index->slot_names[slot], (struct Slice*)name)) {
    slot = (slot + 1) & mask;
  }
  return slot;
}

// Double the table; ids are kept, only slot positions change.
static void slice_index_grow(struct SliceIndex* index) {
  const struct Slice** old_names = index->slot_names;
  uint32_t* old_ids = index->slot_ids;
  size_t old_count = index->slot_count;
  slice_index_alloc_slots(index, old_count * 2);
  for (size_t i = 0; i < old_count; i++) {
    if (old_names[i] != NULL) {
      size_t slot = slice_index_probe(index, old_names[i]);
      index->slot_names[slot] = old_names[i];
      index->slot_ids[slot] = old_ids[i];
    }
  }
  free(old_names);
  free(old_ids);
}

uint32_t slice_index_add(struct SliceIndex* index, const struct Slice* name, bool* added) {
  size_t slot = slice_index_probe(index, name);
  if (index->slot_names[slot] != NULL) {
    if (added != NULL) {
      *added = false;
    }
    return index->slot_ids[slot];
  }
  if ((size_t)index->count + 1 > index->slot_count / 2) {
    slice_index_grow(index);
    slot = slice_index_probe(index, name);
  }
  index->slot_names[slot] = name;
  index->slot_ids[slot] = index->count;
  if (added != NULL) {
    *added = true;
  }
  return index->count++;
}

uint32_t slice_index_get(const struct SliceIndex* index, const struct Slice* name) {
  size_t slot = slice_index_probe(index, name);
  return index->slot_names[slot] != NULL ? index->slot_ids[slot] : SLICE_INDEX_NONE;
}
