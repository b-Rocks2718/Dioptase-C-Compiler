#ifndef SOURCE_LOCATION_H
#define SOURCE_LOCATION_H

#include <stdbool.h>
#include <stddef.h>

// One original-file coordinate. Line/column are 1-based; filename points to
// stable storage (the preprocessor's file table).
struct SourceMappingEntry {
  const char* filename;
  size_t line;
  size_t column;
};

// A run of consecutive preprocessed bytes with a regular mapping. Byte
// start + k maps to (filename, line, column + k) when advance is true, and to
// (filename, line, column) for every byte when advance is false (macro
// expansions and synthesized text share the location of their origin).
// A run extends to the next run's start, or to the mapping's length.
struct SourceMapRun {
  size_t start;
  const char* filename;
  size_t line;
  size_t column;
  bool advance;
};

// Map preprocessed output offsets back to original source coordinates as a
// run-length encoding. Runs are sorted by start and runs[0].start == 0 when
// length > 0. length matches the preprocessed buffer length.
struct SourceMapping {
  struct SourceMapRun* runs;
  size_t run_count;
  size_t run_cap;
  size_t length;
};

// Store a source line, column, and byte offset for diagnostics.
struct SourceLocation {
  size_t line;
  size_t column;
  size_t offset;
};

// Append len bytes that begin at preprocessed offset map->length. With advance
// the bytes map to consecutive columns starting at loc; otherwise all map to
// loc. Adjacent compatible runs are merged. Returns false on allocation failure.
bool source_mapping_append(struct SourceMapping* map, struct SourceMappingEntry loc,
                           size_t len, bool advance);

// Append the mapping of src bytes [offset, offset + len) at the end of dst.
// The range must lie within src. Returns false on allocation failure.
bool source_mapping_append_range(struct SourceMapping* dst, const struct SourceMapping* src,
                                 size_t offset, size_t len);

// Return the original coordinate of preprocessed byte offset (< map->length).
struct SourceMappingEntry source_mapping_lookup(const struct SourceMapping* map, size_t offset);

// Release unused run capacity once a mapping is complete.
void source_mapping_shrink(struct SourceMapping* map);

// Free a mapping's runs and reset it to empty.
void source_mapping_free(struct SourceMapping* map);

void set_source_context(const char* filename, const char* text);

void set_source_context_with_map(const char* filename, const char* text, const struct SourceMapping* map);

const char* source_filename(void);

const char* source_filename_for_ptr(const char* ptr);

const char* source_text(void);

const char* source_text_end(void);

struct SourceLocation source_location_from_ptr(const char* ptr);

#endif // SOURCE_LOCATION_H
