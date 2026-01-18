#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>

#include "slice.h"
#include "arena.h"

bool compare_slice_to_pointer(const struct Slice* s, char const *p) {
  for (size_t i = 0; i < s->len; i++) {
    if (p[i] != s->start[i])
      return false;
  }
  return p[s->len] == 0;
}

bool compare_slice_to_slice(const struct Slice* self, const struct Slice* other) {
  if (self->len != other->len)
    return false;
  for (size_t i = 0; i < self->len; i++) {
    if (other->start[i] != self->start[i])
      return false;
  }
  return true;
}

bool is_identifier(const struct Slice* slice) {
  if (slice->len == 0)
    return false;
  if (!isalpha(slice->start[0]))
    return false;
  for (size_t i = 1; i < slice->len; i++)
    if (!isalnum(slice->start[i]))
      return false;
  return true;
}

struct Slice* slice_concat(const struct Slice* a, const char* b) {
  size_t b_len = 0;
  while (b[b_len] != 0) {
    b_len++;
  }

  char* new_str = (char*)arena_alloc(a->len + b_len);
  for (size_t i = 0; i < a->len; i++) {
    new_str[i] = a->start[i];
  }
  for (size_t i = 0; i < b_len; i++) {
    new_str[a->len + i] = b[i];
  }

  struct Slice* slice = (struct Slice*)arena_alloc(sizeof(struct Slice));
  slice->start = new_str;
  slice->len = a->len + b_len;
  return slice;
}

void print_slice(struct Slice* slice) {
  for (size_t i = 0; i < slice->len; i++) {
    printf("%c", slice->start[i]);
  }
}

void print_slice_with_escapes(struct Slice* slice) {
  for (size_t i = 0; i < slice->len; i++) {
    char c = slice->start[i];
    switch (c) {
      case '\a':
        printf("\\a");
        break;
      case '\b':
        printf("\\b");
        break;
      case '\f':
        printf("\\f");
        break;
      case '\n':
        printf("\\n");
        break;
      case '\r':
        printf("\\r");
        break;
      case '\t':
        printf("\\t");
        break;
      case '\v':
        printf("\\v");
        break;
      case '\\':
        printf("\\\\");
        break;
      case '\"':
        printf("\\\"");
        break;
      case '\'':
        printf("\\'");
        break;
      case '\0':
        printf("\\0");
        break;
      default:
        if (isprint((unsigned char)c)) {
          printf("%c", c);
        } else {
          // Print non-printable characters as octal escape sequences
          printf("\\%03o", (unsigned char)c);
        }
        break;
    }
  }
}

size_t hash_slice(const struct Slice* key) {
  // djb2
  size_t out = 5381;
  for (size_t i = 0; i < key->len; i++) {
    char const c = key->start[i];
    out = out * 33 + c;
  }
  return out;
}