/*
 * Verifies that array-declarator backtracking releases its temporary dimension
 * list. Allocation interception is limited to the included parser translation
 * unit; the arena, lexer, and token array use their normal host allocators.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static size_t live_allocations;

static void* tracked_malloc(size_t size) { /* Allocate memory and record the allocation for the test. */
  void* ptr = malloc(size);
  if (ptr != NULL) live_allocations++;
  return ptr;
}

static void tracked_free(void* ptr) { /* Release memory and record the deallocation for the test. */
  if (ptr != NULL) {
    assert(live_allocations > 0 && "free must match a tracked allocation");
    live_allocations--;
  }
  free(ptr);
}

#define malloc tracked_malloc
#define free tracked_free
#include "parser.c"
#include "lexer.h"
#undef malloc
#undef free

int main(void) { /* Exercise parser allocation test behavior. */
  char* cases[] = {
    "int values[bad];",
    "int values[2][bad];",
    "int values[2;",
  };
  const size_t arena_block_size = 4096;

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    arena_init(arena_block_size);
    set_source_context("array-dimensions.c", cases[i]);
    struct TokenArray* tokens = lex(cases[i]);
    assert(tokens != NULL && "regression input must reach the parser");
    struct Program* parsed = parse_prog(tokens);
    assert(parsed == NULL && "malformed array dimensions must be rejected");
    assert(live_allocations == 0 && "backtracking must release dimensions");
    destroy_token_array(tokens);
    arena_destroy();
  }
  return 0;
}
