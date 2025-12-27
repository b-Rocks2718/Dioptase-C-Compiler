#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

struct ArenaBlock {
  struct ArenaBlock* next;
  size_t used;
  size_t cap;
  unsigned char data[];
};

struct Arena {
  struct ArenaBlock* head;
  size_t block_size;
};

void arena_init(struct Arena* arena, size_t block_size);
void* arena_alloc(struct Arena* arena, size_t size);
void arena_destroy(struct Arena* arena);

#endif
