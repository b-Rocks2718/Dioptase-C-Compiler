#include <stdlib.h>
#include <stdio.h>

#include "token_array.h"

// Slices per payload chunk (16 bytes each on 64-bit hosts, so 16 KiB chunks).
enum { kSliceChunkCapacity = 1024 };

// Allocate an empty token array with the requested capacity.
struct TokenArray* create_token_array(size_t capacity){
  struct TokenArray* arr = malloc(sizeof(struct TokenArray));
  struct Token* tokens = malloc(sizeof(struct Token) * capacity);
  if (arr == NULL || tokens == NULL) {
    fprintf(stderr,
            "Lexer memory error: unable to allocate a token array for %zu tokens\n",
            capacity);
    exit(1);
  }

  arr->capacity = capacity;
  arr->size = 0;
  arr->tokens = tokens;
  arr->slices = NULL;

  return arr;
}

// Append a token by value, doubling storage when full.
void token_array_append(struct TokenArray* arr, const struct Token* value){
  if (arr->size == arr->capacity){
    size_t new_capacity = arr->capacity * 2;
    struct Token* grown = realloc(arr->tokens, new_capacity * sizeof(struct Token));
    if (grown == NULL) {
      fprintf(stderr,
              "Lexer memory error: unable to grow the token array from %zu to %zu tokens\n",
              arr->capacity, new_capacity);
      exit(1);
    }
    arr->tokens = grown;
    arr->capacity = new_capacity;
  }

  arr->tokens[arr->size] = *value;
  arr->size++;
}

// Carve a payload slice from the current chunk, starting a new chunk when full.
struct Slice* token_array_new_slice(struct TokenArray* arr, const char* start, size_t len){
  if (arr->slices == NULL || arr->slices->used == arr->slices->cap) {
    struct SliceChunk* chunk =
        malloc(sizeof(struct SliceChunk) + kSliceChunkCapacity * sizeof(struct Slice));
    if (chunk == NULL) return NULL;
    chunk->next = arr->slices;
    chunk->used = 0;
    chunk->cap = kSliceChunkCapacity;
    arr->slices = chunk;
  }
  struct Slice* slice = &arr->slices->slices[arr->slices->used++];
  slice->start = start;
  slice->len = len;
  return slice;
}

// Trim token storage to the final token count. Failure to shrink is harmless.
void token_array_shrink(struct TokenArray* arr){
  if (arr->size == 0 || arr->size == arr->capacity) return;
  struct Token* shrunk = realloc(arr->tokens, arr->size * sizeof(struct Token));
  if (shrunk != NULL) {
    arr->tokens = shrunk;
    arr->capacity = arr->size;
  }
}

// Free token entries; payload slices remain owned by arr.
void token_array_release_tokens(struct TokenArray* arr){
  free(arr->tokens);
  arr->tokens = NULL;
  arr->size = 0;
  arr->capacity = 0;
}

// Return the token at an already-validated array index.
struct Token token_array_get(struct TokenArray* arr, size_t i){
  // no checks on i, might regret this later
  return arr->tokens[i];
}

// Free the token array, its payload slices, and its token storage.
void destroy_token_array(struct TokenArray* arr){
  struct SliceChunk* chunk = arr->slices;
  while (chunk != NULL) {
    struct SliceChunk* next = chunk->next;
    free(chunk);
    chunk = next;
  }
  free(arr->tokens);
  free(arr);
}

// Print the token array in lexer-debug format.
void print_token_array(struct TokenArray* arr){
  printf("[");
  for (int i = 0; i < arr->size; ++i){
    if (i % 10 == 0) printf("\n    ");
    print_token(arr->tokens[i]);
    if (i == arr->size - 1) printf("\n]\n");
    else printf(", ");
  }
}
