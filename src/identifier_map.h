#ifndef IDENTIFIER_MAP_H
#define IDENTIFIER_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "slice.h"

struct IdentStack {
  struct IdentMap** maps;
  size_t size;
  size_t capacity;
};

struct IdentMapEntry{
  struct Slice* key;
  struct Slice* entry_name;
  bool has_linkage;
  struct IdentMapEntry* next;
};

struct IdentMap{
	size_t size;
  struct IdentMapEntry** arr;
};

struct IdentStack* create_ident_stack(size_t initial_capacity);

struct IdentStack* init_scope(void);

void enter_scope(struct IdentStack* stack);

void exit_scope(struct IdentStack* stack);

struct IdentMapEntry* ident_stack_get(struct IdentStack* stack, struct Slice* key, bool* from_current_scope);

bool ident_stack_in_current_scope(struct IdentStack* stack, struct Slice* key);

void ident_stack_insert(struct IdentStack* stack, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage);

void destroy_ident_stack(struct IdentStack* stack);

struct IdentMapEntry* ident_map_get(struct IdentMap* hmap, struct Slice* key);

void ident_map_insert(struct IdentMap* hmap, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage);

struct IdentMap* create_ident_map(size_t size);

void destroy_ident_map(struct IdentMap* hmap);

#endif // IDENTIFIER_MAP_H