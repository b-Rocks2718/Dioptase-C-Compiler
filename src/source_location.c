#include "source_location.h"

#include <string.h>

static const char* source_text_ptr = NULL;
static const char* source_file_ptr = NULL;
static const struct SourceMapping* source_map_ptr = NULL;

void set_source_context(const char* filename, const char* text) {
  set_source_context_with_map(filename, text, NULL);
}

void set_source_context_with_map(const char* filename, const char* text, const struct SourceMapping* map) {
  source_file_ptr = filename;
  source_text_ptr = text;
  source_map_ptr = map;
}

const char* source_filename(void) {
  return source_file_ptr ? source_file_ptr : "<input>";
}

const char* source_filename_for_ptr(const char* ptr) {
  if (source_map_ptr == NULL || source_text_ptr == NULL || ptr == NULL) {
    return source_filename();
  }

  const char* end = source_text_end();
  if (end != NULL && ptr > end) ptr = end;
  if (ptr < source_text_ptr) return source_filename();

  size_t offset = (size_t)(ptr - source_text_ptr);
  if (offset >= source_map_ptr->length) {
    if (source_map_ptr->length == 0) return source_filename();
    offset = source_map_ptr->length - 1;
  }
  const char* mapped = source_map_ptr->entries[offset].filename;
  return mapped != NULL ? mapped : source_filename();
}

const char* source_text(void) {
  return source_text_ptr;
}

const char* source_text_end(void) {
  if (source_text_ptr == NULL) return NULL;
  return source_text_ptr + strlen(source_text_ptr);
}

struct SourceLocation source_location_from_ptr(const char* ptr) {
  struct SourceLocation loc = {0, 0, 0};
  if (source_text_ptr == NULL || ptr == NULL) return loc;

  const char* end = source_text_end();
  if (end != NULL && ptr > end) ptr = end;
  if (ptr < source_text_ptr) return loc;

  if (source_map_ptr != NULL && source_map_ptr->entries != NULL && source_map_ptr->length > 0) {
    size_t offset = (size_t)(ptr - source_text_ptr);
    if (offset >= source_map_ptr->length) {
      offset = source_map_ptr->length - 1;
    }
    loc.line = source_map_ptr->entries[offset].line;
    loc.column = source_map_ptr->entries[offset].column;
    loc.offset = offset;
    return loc;
  }

  size_t line = 1;
  size_t column = 1;
  const char* cur = source_text_ptr;

  // Walk the buffer to compute a 1-based line/column for ptr.
  while (cur < ptr && *cur != '\0') {
    if (*cur == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
    cur++;
  }

  loc.line = line;
  loc.column = column;
  loc.offset = (size_t)(ptr - source_text_ptr);
  return loc;
}
