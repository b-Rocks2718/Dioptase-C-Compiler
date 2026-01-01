#ifndef SOURCE_LOCATION_H
#define SOURCE_LOCATION_H

#include <stddef.h>

struct SourceLocation {
  size_t line;
  size_t column;
  size_t offset;
};

void set_source_context(const char* filename, const char* text);

const char* source_filename(void);

const char* source_text(void);

const char* source_text_end(void);

struct SourceLocation source_location_from_ptr(const char* ptr);

#endif // SOURCE_LOCATION_H
