#include "label_map.h"
#include "exit_codes.h"

#include <stdio.h>
#include <stdlib.h>

struct LabelMap* create_label_map(size_t num_buckets) {
  struct LabelMap* hmap = malloc(sizeof(struct LabelMap));
  if (hmap == NULL) {
    fprintf(stderr, "Label map error: unable to allocate a %zu-bucket map\n", num_buckets);
    exit(BCC_EXIT_INTERNAL);
  }
  slice_map_init(&hmap->map, num_buckets, true);
  return hmap;
}

void label_map_insert(struct LabelMap* hmap, struct Slice* key, struct Slice* value) {
  slice_map_put(&hmap->map, key, value);
}

struct Slice* label_map_get(struct LabelMap* hmap, struct Slice* key) {
  return slice_map_get(&hmap->map, key);
}

bool label_map_contains(struct LabelMap* hmap, struct Slice* key) {
  return slice_map_contains(&hmap->map, key);
}

void destroy_label_map(struct LabelMap* hmap) {
  slice_map_free(&hmap->map);
  free(hmap);
}
