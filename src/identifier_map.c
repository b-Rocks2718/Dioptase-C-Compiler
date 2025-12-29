#include <stdlib.h>

#include "identifier_map.h"
#include "slice.h"

#define IDENT_MAP_BUCKETS 1024
#define INITIAL_STACK_CAPACITY 8

struct IdentStack* init_scope(void) {
  struct IdentStack* stack = create_ident_stack(INITIAL_STACK_CAPACITY);
  enter_scope(stack);
  return stack;
}

struct IdentStack* create_ident_stack(size_t initial_capacity){
  struct IdentMap** maps = malloc(initial_capacity * sizeof(struct IdentMap*));
  struct IdentStack* stack = malloc(sizeof(struct IdentStack));

  stack->maps = maps;
  stack->size = 0;
  stack->capacity = initial_capacity;

  return stack;
}

void ident_stack_push(struct IdentStack* stack, struct IdentMap* map){
  if (stack->size >= stack->capacity){
    size_t new_capacity = stack->capacity * 2;
    struct IdentMap** new_maps = malloc(new_capacity * sizeof(struct IdentMap*));
    for (int i = 0; i < stack->size; ++i){
      new_maps[i] = stack->maps[i];
    }
    free(stack->maps);
    stack->maps = new_maps;
    stack->capacity = new_capacity;
  }
  stack->maps[stack->size] = map;
  stack->size += 1;
}

struct IdentMap* ident_stack_pop(struct IdentStack* stack){
  if (stack->size == 0){
    return NULL;
  } else {
    stack->size -= 1;
    return stack->maps[stack->size];
  }
}

struct IdentMap* ident_stack_peek(struct IdentStack* stack){
  if (stack->size == 0){
    return NULL;
  } else {
    return stack->maps[stack->size - 1];
  }
}

struct IdentMapEntry* ident_stack_get(struct IdentStack* stack, struct Slice* key, bool* from_current_scope){
  for (int i = stack->size - 1; i >= 0; --i){
    struct IdentMapEntry* entry = ident_map_get(stack->maps[i], key);
    if (entry != NULL){
      *from_current_scope = (i == stack->size - 1);
      return entry;
    }
  }
  return NULL;
}

bool ident_stack_in_current_scope(struct IdentStack* stack, struct Slice* key){
  struct IdentMap* current_map = ident_stack_peek(stack);
  if (current_map != NULL){
    struct IdentMapEntry* entry = ident_map_get(current_map, key);
    return (entry != NULL);
  } else {
    // error: no map to check
    printf("Identifier Map Error: No map in stack to check\n");
    return false;
  }
}

void ident_stack_insert(struct IdentStack* stack, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage){
  struct IdentMap* current_map = ident_stack_peek(stack);
  if (current_map != NULL){
    ident_map_insert(current_map, key, entry_name, has_linkage);
  } else {
    // error: no map to insert into
    printf("Identifier Map Error: No map in stack to insert into\n");
  }
}

void enter_scope(struct IdentStack* stack){
  struct IdentMap* new_map = create_ident_map(IDENT_MAP_BUCKETS);
  ident_stack_push(stack, new_map);
}

void exit_scope(struct IdentStack* stack){
  struct IdentMap* old_map = ident_stack_pop(stack);
  if (old_map != NULL){
    destroy_ident_map(old_map);
  } else {
    // error: no map to pop
    printf("Identifier Map Error: No map in stack to pop\n");
  }
}

void destroy_ident_stack(struct IdentStack* stack){
  for (int i = 0; i < stack->size; ++i){
    destroy_ident_map(stack->maps[i]);
  }
  free(stack->maps);
  free(stack);
}

struct IdentMap* create_ident_map(size_t num_buckets){
  struct IdentMapEntry** arr = malloc(num_buckets * sizeof(struct IdentMapEntry*));
  struct IdentMap* hmap = malloc(sizeof(struct IdentMap));

  for (int i = 0; i < num_buckets; ++i){
    arr[i] = NULL;
  }

  hmap->size = num_buckets;
  hmap->arr = arr;

  return hmap;
}

struct IdentMapEntry* create_ident_map_entry(struct Slice* key, 
    struct Slice* entry_name, bool has_linkage){
  struct IdentMapEntry* entry = malloc(sizeof(struct IdentMapEntry));

  entry->key = key;
  entry->entry_name = entry_name;
  entry->has_linkage = has_linkage;
  entry->next = NULL;

  return entry;
}

void ident_map_entry_insert(struct IdentMapEntry* entry, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage){
  if (compare_slice_to_slice(entry->key, key)){
    entry->entry_name = entry_name;
    entry->has_linkage = has_linkage;
    free(key);
  } else if (entry->next == NULL){
    entry->next = create_ident_map_entry(key, entry_name, has_linkage);
  } else {
    ident_map_entry_insert(entry->next, key, entry_name, has_linkage);
  }
}

void ident_map_insert(struct IdentMap* hmap, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage){
  size_t hash = hash_slice(key) % hmap->size;
  
  if ((hmap->arr[hash]) == NULL){
    hmap->arr[hash] = create_ident_map_entry(key, entry_name, has_linkage);
  } else {
    ident_map_entry_insert(hmap->arr[hash], key, entry_name, has_linkage);
  }
}

struct IdentMapEntry* ident_map_entry_get(struct IdentMapEntry* entry, struct Slice* key){
  if (compare_slice_to_slice(entry->key, key)){
    return entry;
  } else if (entry->next == NULL){
    return NULL;
  } else {
    return ident_map_entry_get(entry->next, key);
  }
}

struct IdentMapEntry* ident_map_get(struct IdentMap* hmap, struct Slice* key){
  size_t hash = hash_slice(key) % hmap->size;

  if (hmap->arr[hash] == NULL){
    return NULL;
  } else {
    return ident_map_entry_get(hmap->arr[hash], key);
  }
}

void destroy_ident_map_entry(struct IdentMapEntry* entry){
  if (entry->next !=  NULL) destroy_ident_map_entry(entry->next);
  free(entry->key);
  free(entry);
}

void destroy_ident_map(struct IdentMap* hmap){
  for (int i = 0; i < hmap->size; ++i){
    if (hmap->arr[i] != NULL) destroy_ident_map_entry(hmap->arr[i]);
  }
  free(hmap->arr);
  free(hmap);
}
