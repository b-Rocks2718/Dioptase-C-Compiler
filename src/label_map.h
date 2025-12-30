#ifndef LABEL_MAP_H
#define LABEL_MAP_H

#include <stdint.h>

#include "slice.h"

struct LabelEntry{
  struct Slice* key;
  struct Slice* value;
  struct LabelEntry* next;
};

struct LabelMap{
	size_t size;
  struct LabelEntry** arr;
};

struct LabelMap* create_label_map(size_t numBuckets);

void label_map_insert(struct LabelMap* hmap, struct Slice* key, struct Slice* value);

struct Slice* label_map_get(struct LabelMap* hmap, struct Slice* key);

bool label_map_contains(struct LabelMap* hmap, struct Slice* key);

void destroy_label_map(struct LabelMap* hmap);

#endif // LABEL_MAP_H