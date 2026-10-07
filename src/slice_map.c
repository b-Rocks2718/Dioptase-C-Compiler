#include "slice_map.h"
#include "arena.h"
#include "exit_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Allocate map storage from the map's source; heap failures are fatal.
static void* slice_map_alloc(const struct SliceMap* map, size_t size) {
  void* memory = map->heap ? malloc(size) : arena_alloc(size);
  if (memory == NULL) {
    fprintf(stderr, "Slice map error: unable to allocate %zu bytes\n", size);
    exit(BCC_EXIT_INTERNAL);
  }
  return memory;
}

void slice_map_init(struct SliceMap* map, size_t bucket_count, bool heap) {
  map->heap = heap;
  map->bucket_count = bucket_count;
  map->buckets = slice_map_alloc(map, bucket_count * sizeof(*map->buckets));
  memset(map->buckets, 0, bucket_count * sizeof(*map->buckets));
}

void slice_map_free(struct SliceMap* map) {
  if (map->heap && map->buckets != NULL) {
    for (size_t i = 0; i < map->bucket_count; i++) {
      struct SliceMapEntry* entry = map->buckets[i];
      while (entry != NULL) {
        struct SliceMapEntry* next = entry->next;
        free(entry);
        entry = next;
      }
    }
    free(map->buckets);
  }
  map->buckets = NULL;
  map->bucket_count = 0;
}

// First entry with key, or NULL.
static struct SliceMapEntry* slice_map_find(const struct SliceMap* map, const struct Slice* key) {
  for (struct SliceMapEntry* entry = map->buckets[hash_slice(key) % map->bucket_count];
       entry != NULL; entry = entry->next) {
    if (compare_slice_to_slice(entry->key, (struct Slice*)key)) {
      return entry;
    }
  }
  return NULL;
}

void* slice_map_get(const struct SliceMap* map, const struct Slice* key) {
  struct SliceMapEntry* entry = slice_map_find(map, key);
  return entry != NULL ? entry->value : NULL;
}

bool slice_map_contains(const struct SliceMap* map, const struct Slice* key) {
  return slice_map_find(map, key) != NULL;
}

void slice_map_add(struct SliceMap* map, struct Slice* key, void* value) {
  struct SliceMapEntry* entry = slice_map_alloc(map, sizeof(*entry));
  entry->key = key;
  entry->value = value;
  entry->next = NULL;
  struct SliceMapEntry** link = &map->buckets[hash_slice(key) % map->bucket_count];
  while (*link != NULL) {
    link = &(*link)->next;
  }
  *link = entry;
}

void slice_map_put(struct SliceMap* map, struct Slice* key, void* value) {
  struct SliceMapEntry* entry = slice_map_find(map, key);
  if (entry != NULL) {
    entry->value = value;
  } else {
    slice_map_add(map, key, value);
  }
}

struct SliceMapIter slice_map_iter(const struct SliceMap* map) {
  struct SliceMapIter it = { map, 0, NULL };
  return it;
}

bool slice_map_next(struct SliceMapIter* it, struct Slice** key, void** value) {
  if (it->entry != NULL) {
    it->entry = it->entry->next;
  }
  while (it->entry == NULL) {
    if (it->bucket >= it->map->bucket_count) {
      return false;
    }
    it->entry = it->map->buckets[it->bucket++];
  }
  if (key != NULL) {
    *key = it->entry->key;
  }
  if (value != NULL) {
    *value = it->entry->value;
  }
  return true;
}

void* slice_map_next_value(struct SliceMapIter* it) {
  void* value;
  return slice_map_next(it, NULL, &value) ? value : NULL;
}
