#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "arena.h"

// Smallest block size accepted by arena constructors.
enum { kArenaMinBlockSize = 1024 };

struct Arena* arena = NULL;

// Target of arena_alloc. NULL means the compilation arena.
static struct Arena* current_arena = NULL;

// Round a value up to the requested alignment.
static size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// Allocate and initialize an empty arena header.
struct Arena* arena_create(size_t block_size) {
  struct Arena* a = (struct Arena*)malloc(sizeof(struct Arena));
  if (a == NULL) {
    fprintf(stderr, "Arena error: unable to allocate an arena header (%zu bytes)\n",
            sizeof(struct Arena));
    exit(1);
  }
  a->head = NULL;
  a->free_blocks = NULL;
  if (block_size < kArenaMinBlockSize) block_size = kArenaMinBlockSize;
  a->block_size = block_size;
  return a;
}

// Initialize the global arena with a minimum allocation-block size.
void arena_init(size_t block_size) {
  arena = arena_create(block_size);
  current_arena = NULL;
}

// Take a retained block with at least cap bytes from a's free list, or NULL.
static struct ArenaBlock* take_free_block(struct Arena* a, size_t cap) {
  struct ArenaBlock** link = &a->free_blocks;
  while (*link != NULL) {
    struct ArenaBlock* block = *link;
    if (block->cap >= cap) {
      *link = block->next;
      return block;
    }
    link = &block->next;
  }
  return NULL;
}

// Reserve aligned storage from a, starting a new block when the current one
// cannot hold size bytes. Retained blocks are reused before calling malloc.
static void* arena_alloc_from(struct Arena* a, size_t size) {
  if (a == NULL) return NULL;
  size_t alignment = sizeof(void*);
  size = align_up(size, alignment);
  if (size == 0) size = alignment;

  if (a->head == NULL || a->head->used + size > a->head->cap) {
    size_t cap = a->block_size;
    if (cap < size) cap = size;
    struct ArenaBlock* block = take_free_block(a, cap);
    if (block == NULL) {
      block = malloc(sizeof(struct ArenaBlock) + cap);
      if (block == NULL) return NULL;
      block->cap = cap;
    }
    block->next = a->head;
    block->used = 0;
    a->head = block;
  }

  void* out = a->head->data + a->head->used;
  a->head->used += size;
  return out;
}

// Reserve storage from the currently selected arena.
void* arena_alloc(size_t size) {
  return arena_alloc_from(current_arena != NULL ? current_arena : arena, size);
}

// Reserve storage from the compilation arena regardless of the selection.
void* arena_alloc_persistent(size_t size) {
  return arena_alloc_from(arena, size);
}

// Move every in-use block onto the free list. Contents become invalid.
void arena_reset(struct Arena* a) {
  if (a == NULL) return;
  while (a->head != NULL) {
    struct ArenaBlock* block = a->head;
    a->head = block->next;
    block->next = a->free_blocks;
    a->free_blocks = block;
  }
}

// Free a block chain.
static void free_block_chain(struct ArenaBlock* block) {
  while (block != NULL) {
    struct ArenaBlock* next = block->next;
    free(block);
    block = next;
  }
}

// Free an arena and all blocks it owns. Deselects it if it is current.
void arena_free(struct Arena* a) {
  if (a == NULL) return;
  if (current_arena == a) current_arena = NULL;
  free_block_chain(a->head);
  free_block_chain(a->free_blocks);
  free(a);
}

// Select the arena used by arena_alloc; NULL selects the compilation arena.
struct Arena* arena_set_current(struct Arena* a) {
  struct Arena* previous = current_arena;
  current_arena = a;
  return previous;
}

// Free every allocation block owned by the compiler's global arena.
void arena_destroy(void) {
  if (arena == NULL) return;
  arena_free(arena);
  arena = NULL;
  current_arena = NULL;
}
