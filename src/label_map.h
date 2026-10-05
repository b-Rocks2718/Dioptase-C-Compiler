#ifndef LABEL_MAP_H
#define LABEL_MAP_H

#include <stdbool.h>
#include <stddef.h>

#include "slice.h"
#include "slice_map.h"

// Map from a label or variable name to its replacement name, used to give goto
// labels unique names and to rename an inlined body. Keys and values are
// borrowed slices; the map owns only its own heap storage.
struct LabelMap {
  struct SliceMap map;
};

// Allocate an empty map with the given bucket count; release it with
// destroy_label_map.
struct LabelMap* create_label_map(size_t num_buckets);

// Map key to value, replacing any existing mapping for key.
void label_map_insert(struct LabelMap* hmap, struct Slice* key, struct Slice* value);

// The value mapped to key, or NULL if missing.
struct Slice* label_map_get(struct LabelMap* hmap, struct Slice* key);

bool label_map_contains(struct LabelMap* hmap, struct Slice* key);

// Free the map's storage; the slices it refers to are not freed.
void destroy_label_map(struct LabelMap* hmap);

#endif // LABEL_MAP_H
