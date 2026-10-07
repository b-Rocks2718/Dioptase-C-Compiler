#ifndef SLICE_MAP_H
#define SLICE_MAP_H

#include "slice.h"

#include <stdbool.h>
#include <stddef.h>

// Chained hash map from Slice keys to caller-owned values, shared by the
// compiler's symbol, type, identifier, label, and pseudo tables.
//
// The bucket count is fixed at creation and entries are appended at the end of
// their bucket, so iteration order (bucket by bucket, then insertion order)
// is stable. Debug printers rely on that order, which is why the map never
// rehashes.

// One key/value pair in a bucket chain.
struct SliceMapEntry {
  struct Slice* key;
  void* value;
  struct SliceMapEntry* next;
};

struct SliceMap {
  struct SliceMapEntry** buckets;
  size_t bucket_count;
  // Storage source: the arena (arena_alloc, the currently selected arena) or
  // the heap, which slice_map_free releases.
  bool heap;
};

// Initialize an empty map with a fixed bucket count.
void slice_map_init(struct SliceMap* map, size_t bucket_count, bool heap);

// Release a heap map's buckets and entries (not keys or values). Arena maps
// are reclaimed with their arena; calling this on one only clears it.
void slice_map_free(struct SliceMap* map);

// Value of the first entry with key, or NULL when absent.
void* slice_map_get(const struct SliceMap* map, const struct Slice* key);

bool slice_map_contains(const struct SliceMap* map, const struct Slice* key);

// Append key -> value even if key is present; lookups keep returning the
// earliest entry. Matches tables that never overwrite (symbol tables).
void slice_map_add(struct SliceMap* map, struct Slice* key, void* value);

// Replace the value of the first entry with key, or append a new entry.
void slice_map_put(struct SliceMap* map, struct Slice* key, void* value);

// Cursor for visiting every entry in iteration order.
struct SliceMapIter {
  const struct SliceMap* map;
  size_t bucket;
  struct SliceMapEntry* entry;
};

struct SliceMapIter slice_map_iter(const struct SliceMap* map);

// Advance to the next entry; returns false when every entry has been visited.
bool slice_map_next(struct SliceMapIter* it, struct Slice** key, void** value);

// The next entry's value, or NULL when every entry has been visited. For maps
// whose values are never NULL, this allows
//   for (T* v = slice_map_next_value(&it); v != NULL; v = slice_map_next_value(&it))
void* slice_map_next_value(struct SliceMapIter* it);

#endif // SLICE_MAP_H
