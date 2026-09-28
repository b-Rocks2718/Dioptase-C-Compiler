#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

// Own one linked arena allocation block and its used capacity.
struct ArenaBlock {
  struct ArenaBlock* next;
  size_t used;
  size_t cap;
  unsigned char data[];
};

// Own an arena's block chain and default block size. Blocks released by
// arena_reset stay on free_blocks and are reused before new memory is requested.
struct Arena {
  struct ArenaBlock* head;        // block currently being carved, then older blocks
  struct ArenaBlock* free_blocks; // blocks retained by arena_reset for reuse
  size_t block_size;
};

// The compilation-lifetime arena. AST, symbol tables, TAC, and assembly data
// live here until arena_destroy.
extern struct Arena* arena;

// Create the compilation arena and make it the current allocation target.
void arena_init(size_t block_size);

// Allocate from the current arena: the compilation arena unless a phase has
// temporarily selected a scratch arena with arena_set_current.
void* arena_alloc(size_t size);

// Allocate from the compilation arena even while a scratch arena is current.
// Use for data that must outlive the phase using the scratch arena.
void* arena_alloc_persistent(size_t size);

// Free the compilation arena. Scratch arenas are owned and freed separately.
void arena_destroy(void);

// Create an independent arena (for example, optimizer scratch storage).
struct Arena* arena_create(size_t block_size);

// Invalidate every allocation in a, keeping its blocks for reuse.
void arena_reset(struct Arena* a);

// Free an arena created by arena_create, including retained blocks.
void arena_free(struct Arena* a);

// Select the arena used by arena_alloc and return the previous selection.
// Passing NULL selects the compilation arena.
struct Arena* arena_set_current(struct Arena* a);

#endif
