#ifndef IDENTIFIER_MAP_H
#define IDENTIFIER_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "slice.h"
#include "types.h"

// Provide scoped identifier lookup for identifier resolution.
// Supports stack-based scoping and hash map operations.
// Maps store pointers to slices; memory is owned elsewhere.

// Stack of identifier maps, one per scope.
// Used by identifier resolution to search from inner to outer scope.
struct IdentStack {
  struct IdentMap** maps;
  size_t size;
  size_t capacity;
};

// Entry in an identifier hash map.
// Stored in IdentMap buckets for lookups.
struct IdentMapEntry{
  struct Slice* key;
  struct Slice* entry_name;
  bool has_linkage; // used by var map
  bool is_const; // used by var map
  unsigned value; // used by var map for enum constant value
  enum TypeType type; // used by type map
  struct IdentMapEntry* next;
};

// Hash map of identifiers for a single scope.
// Supports scoped identifier lookup.
struct IdentMap{
	size_t size;
  struct IdentMapEntry** arr;
};

// Create an identifier stack with a given initial capacity.
// Returns an arena-allocated IdentStack with no scopes.
struct IdentStack* create_ident_stack(size_t initial_capacity);

// Create a resolver stack containing one empty scope map.
// Uses the default bucket count; the compiler arena
// owns the returned stack and map.
struct IdentStack* init_scope(void);

// Push a new empty scope onto the stack.
// Adds a new IdentMap on top of the stack.
void enter_scope(struct IdentStack* stack);

// Pop the top scope from the stack.
// Removes the current IdentMap and returns it.
// The compiler arena continues to own the returned map;
// no stack lookups occur in that scope afterward.
struct IdentMap* exit_scope(struct IdentStack* stack);

// Look up an identifier across scopes.
// Returns the entry and sets from_current_scope accordingly.
struct IdentMapEntry* ident_stack_get(struct IdentStack* stack, struct Slice* key, bool* from_current_scope);

// Check if an identifier exists in the current scope.
// Returns true if found in the top scope.
bool ident_stack_in_current_scope(struct IdentStack* stack, struct Slice* key);

// Insert an identifier mapping into the current scope.
void ident_stack_insert(struct IdentStack* stack, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage, enum TypeType type, bool is_const, unsigned value);

// Look up an identifier in a single scope map.
// Returns the entry or NULL if missing.
struct IdentMapEntry* ident_map_get(struct IdentMap* hmap, struct Slice* key);

// Insert or update an identifier mapping in a scope map.
void ident_map_insert(struct IdentMap* hmap, struct Slice* key, 
    struct Slice* entry_name, bool has_linkage, enum TypeType type, bool is_const, unsigned value);

// Create a new identifier map with a given bucket count.
// Returns an arena-allocated IdentMap.
// The compiler arena is initialized and owns all map
// storage until arena_destroy.
struct IdentMap* create_ident_map(size_t size);

#endif // IDENTIFIER_MAP_H
