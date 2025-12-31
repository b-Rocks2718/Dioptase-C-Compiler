#include "label_map.h"

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "slice.h"

struct LabelMap* create_label_map(size_t num_buckets){
  struct LabelEntry** arr = malloc(num_buckets * sizeof(struct LabelEntry*));
  struct LabelMap* hmap = malloc(sizeof(struct LabelMap));

  for (int i = 0; i < num_buckets; ++i){
    arr[i] = NULL;
  }

  hmap->size = num_buckets;
  hmap->arr = arr;

  return hmap;
}

struct LabelEntry* create_label_entry(struct Slice* key, struct Slice* value){
  struct LabelEntry* entry = malloc(sizeof(struct LabelEntry));

  entry->key = key;
  entry->value = value;
  entry->next = NULL;

  return entry;
}

void label_entry_insert(struct LabelEntry* entry, struct Slice* key, struct Slice* value){
  if (compare_slice_to_slice(entry->key, key)){
    entry->value = value;
  } else if (entry->next == NULL){
    entry->next = create_label_entry(key, value);
  } else {
    label_entry_insert(entry->next, key, value);
  }
}

void label_map_insert(struct LabelMap* hmap, struct Slice* key, struct Slice* value){
  size_t label = hash_slice(key) % hmap->size;
  
  if ((hmap->arr[label]) == NULL){
    hmap->arr[label] = create_label_entry(key, value);
  } else {
    label_entry_insert(hmap->arr[label], key, value);
  }
}

struct Slice* label_entry_get(struct LabelEntry* entry, struct Slice* key){
  if (compare_slice_to_slice(entry->key, key)){
    return entry->value;
  } else if (entry->next == NULL){
    return 0;
  } else {
    return label_entry_get(entry->next, key);
  }
}

struct Slice* label_map_get(struct LabelMap* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  if (hmap->arr[label] == NULL){
    return 0;
  } else {
    return label_entry_get(hmap->arr[label], key);
  }
}

bool label_entry_contains(struct LabelEntry* entry, struct Slice* key){
  if (compare_slice_to_slice(entry->key, key)){
    return true;
  } else if (entry->next == NULL){
    return false;
  } else {
    return label_entry_contains(entry->next, key);
  }
}

bool label_map_contains(struct LabelMap* hmap, struct Slice* key){
  size_t label = hash_slice(key) % hmap->size;

  if (hmap->arr[label] == NULL){
    return false;
  } else {
    return label_entry_contains(hmap->arr[label], key);
  }
}

void destroy_label_entry(struct LabelEntry* entry){
  if (entry->next !=  NULL) destroy_label_entry(entry->next);
  free(entry);
}

void destroy_label_map(struct LabelMap* hmap){
  for (int i = 0; i < hmap->size; ++i){
    if (hmap->arr[i] != NULL) destroy_label_entry(hmap->arr[i]);
  }
  free(hmap->arr);
  free(hmap);
}

