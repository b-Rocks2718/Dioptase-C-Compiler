#include "source_location.h"

#include <string.h>

static const char* source_text_ptr = NULL;
static const char* source_file_ptr = NULL;

void set_source_context(const char* filename, const char* text) {
  source_file_ptr = filename;
  source_text_ptr = text;
}

const char* source_filename(void) {
  return source_file_ptr ? source_file_ptr : "<input>";
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
