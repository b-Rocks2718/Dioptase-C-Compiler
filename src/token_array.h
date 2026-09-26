#ifndef TOKEN_ARRAY_H
#define TOKEN_ARRAY_H

#include <stddef.h>

#include "slice.h"
#include "token.h"

// A chunk of identifier and string-literal payload slices. Chunks never move,
// so slice addresses stay valid after the token array itself is released.
struct SliceChunk {
  struct SliceChunk* next;
  size_t used;
  size_t cap;
  struct Slice slices[];
};

// Own the token storage and the payload slices that tokens (and, after
// parsing, the AST) point to.
struct TokenArray {
  struct Token* tokens;       // NULL after token_array_release_tokens
  size_t size;
  size_t capacity;
  struct SliceChunk* slices;  // freed only by destroy_token_array
};

struct TokenArray* create_token_array(size_t capacity);

// Append a copy of value, growing storage as needed.
void token_array_append(struct TokenArray* arr, const struct Token* value);

// Allocate a payload slice owned by arr. Returns NULL on allocation failure.
struct Slice* token_array_new_slice(struct TokenArray* arr, const char* start, size_t len);

// Trim token storage to exactly size entries once lexing is complete.
void token_array_shrink(struct TokenArray* arr);

// Free the token entries once parsing is complete. Payload slices referenced
// by the AST stay valid until destroy_token_array.
void token_array_release_tokens(struct TokenArray* arr);

struct Token token_array_get(struct TokenArray* arr, size_t i);

// Free the token entries (if still present), every payload slice, and arr.
void destroy_token_array(struct TokenArray* arr);

void print_token_array(struct TokenArray* arr);

#endif // TOKEN_ARRAY_H
