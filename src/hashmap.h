#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>

#include "slice.h"

struct HashEntry{
  struct Slice* key;
  uint64_t value;
  struct HashEntry* next;
};

struct HashMap{
	size_t size;
  struct HashEntry** arr;
};

struct HashMap* create_hash_map(size_t numBuckets);

void hash_map_insert(struct HashMap* hmap, struct Slice* key, uint64_t value);

uint64_t hash_map_get(struct HashMap* hmap, struct Slice* key);

void destroy_hash_map(struct HashMap* hmap);

#endif // HASHMAP_H