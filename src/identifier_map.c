#include <stdlib.h>

#include "arena.h"
#include "exit_codes.h"
#include "identifier_map.h"
#include "slice.h"

// Implement scoped identifier maps for name resolution.
// Provides scope stack and hash map operations.
// All storage is compiler-arena-owned. Maps store pointers
// to slices whose lifetime is at least as long as the identifier-resolution pass.

// Default bucket count for per-scope identifier maps.
// Controls hash table size.
#define IDENT_MAP_BUCKETS 16
// Default initial capacity for the scope stack.
// Controls how many scopes fit before resizing.
#define INITIAL_STACK_CAPACITY 8

// Create the resolver's scope stack with its initial file or function scope.
struct IdentStack* init_scope(void) {
  struct IdentStack* stack = create_ident_stack(INITIAL_STACK_CAPACITY);
  enter_scope(stack);
  return stack;
}

// Allocate an identifier stack with a given capacity.
// Returns an arena-allocated IdentStack with no scopes.
// The compiler arena is initialized; callers must push a
// scope before lookup/insert.
struct IdentStack* create_ident_stack(size_t initial_capacity){
  struct IdentMap** maps = arena_alloc(initial_capacity * sizeof(struct IdentMap*));
  struct IdentStack* stack = arena_alloc(sizeof(struct IdentStack));

  stack->maps = maps;
  stack->size = 0;
  stack->capacity = initial_capacity;

  return stack;
}

// Push a map onto the scope stack, resizing if needed.
// Adds map to the top of the stack.
// Stack grows by doubling capacity. Superseded arrays
// remain in the arena until the compilation finishes.
void ident_stack_push(struct IdentStack* stack, struct IdentMap* map){
  if (stack->size >= stack->capacity){
    size_t new_capacity = stack->capacity * 2;
    struct IdentMap** new_maps = arena_alloc(new_capacity * sizeof(struct IdentMap*));
    for (int i = 0; i < stack->size; ++i){
      new_maps[i] = stack->maps[i];
    }
    stack->maps = new_maps;
    stack->capacity = new_capacity;
  }
  stack->maps[stack->size] = map;
  stack->size += 1;
}

// Pop the top scope map from the stack.
// Returns the popped IdentMap or NULL if the stack is empty.
struct IdentMap* ident_stack_pop(struct IdentStack* stack){
  if (stack->size == 0){
    return NULL;
  } else {
    stack->size -= 1;
    return stack->maps[stack->size];
  }
}

// Peek at the current scope map without removing it.
// Returns the top IdentMap or NULL if the stack is empty.
// Returned map remains owned by the stack.
struct IdentMap* ident_stack_peek(struct IdentStack* stack){
  if (stack->size == 0){
    return NULL;
  } else {
    return stack->maps[stack->size - 1];
  }
}

// Look up an identifier across all scopes.
// Returns the entry and sets from_current_scope.
struct IdentMapEntry* ident_stack_get(struct IdentStack* stack, struct Slice* key, bool* from_current_scope){
  for (int i = stack->size - 1; i >= 0; --i){
    struct IdentMapEntry* entry = ident_map_get(stack->maps[i], key);
    if (entry != NULL){
      if (from_current_scope != NULL){
        *from_current_scope = (i == stack->size - 1);
      }
      return entry;
    }
  }
  return NULL;
}

// Check if a name is declared in the current scope.
// Returns true if found in the top scope.
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

// Insert a name mapping into the current scope.
void ident_stack_insert(struct IdentStack* stack, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage, enum TypeType type, bool is_const, unsigned value){
  struct IdentMap* current_map = ident_stack_peek(stack);
  if (current_map != NULL){
    ident_map_insert(current_map, key, entry_name, has_linkage, type, is_const, value);
  } else {
    // error: no map to insert into
    printf("Identifier Map Error: No map in stack to insert into\n");
  }
}

// Push a new empty scope onto the stack.
// Adds a new IdentMap with default bucket count.
void enter_scope(struct IdentStack* stack){
  struct IdentMap* new_map = create_ident_map(IDENT_MAP_BUCKETS);
  ident_stack_push(stack, new_map);
}

// Pop the current scope.
// Returns the top IdentMap and removes it from the stack.
struct IdentMap* exit_scope(struct IdentStack* stack){
  struct IdentMap* old_map = ident_stack_pop(stack);
  if (old_map != NULL){
    return old_map;
  } else {
    // error: no map to pop
    printf("Identifier Map Error: No map in stack to pop\n");
    exit(BCC_EXIT_INTERNAL);
  }
}

// Allocate a new identifier map with a given bucket count.
// Returns an arena-allocated IdentMap.
struct IdentMap* create_ident_map(size_t num_buckets){
  struct IdentMap* hmap = arena_alloc(sizeof(struct IdentMap));
  slice_map_init(&hmap->map, num_buckets, false);
  return hmap;
}

// Insert or update a mapping in the identifier map. An existing entry is
// updated in place so callers holding it see the new meaning.
void ident_map_insert(struct IdentMap* hmap, struct Slice* key,
    struct Slice* entry_name, bool has_linkage, enum TypeType type, bool is_const, unsigned value){
  struct IdentMapEntry* entry = slice_map_get(&hmap->map, key);
  if (entry == NULL) {
    entry = arena_alloc(sizeof(struct IdentMapEntry));
    entry->key = key;
    slice_map_add(&hmap->map, key, entry);
  }
  entry->entry_name = entry_name;
  entry->has_linkage = has_linkage;
  entry->type = type;
  entry->is_const = is_const;
  entry->value = value;
}

// Look up an identifier in the map.
// Returns the entry or NULL if missing.
struct IdentMapEntry* ident_map_get(struct IdentMap* hmap, struct Slice* key){
  return slice_map_get(&hmap->map, key);
}
