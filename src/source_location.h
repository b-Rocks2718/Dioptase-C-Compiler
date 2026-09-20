#ifndef SOURCE_LOCATION_H
#define SOURCE_LOCATION_H

#include <stddef.h>

// The a single source coordinate in the original input stores based, filename, line, column.
// Stored alongside preprocessed output for error reporting.
// line/column are 1-based; filename points to stable storage.
struct SourceMappingEntry {
  const char* filename;
  size_t line;
  size_t column;
};

// Map preprocessed output offsets back to original source coordinates.
// length matches the preprocessed buffer length.
struct SourceMapping {
  struct SourceMappingEntry* entries;
  size_t length;
};

// The source location stores line, column, offset.
struct SourceLocation {
  size_t line;
  size_t column;
  size_t offset;
};

void set_source_context(const char* filename, const char* text);

void set_source_context_with_map(const char* filename, const char* text, const struct SourceMapping* map);

const char* source_filename(void);

const char* source_filename_for_ptr(const char* ptr);

const char* source_text(void);

const char* source_text_end(void);

struct SourceLocation source_location_from_ptr(const char* ptr);

#endif // SOURCE_LOCATION_H
