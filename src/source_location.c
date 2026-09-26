#include "source_location.h"

#include <stdlib.h>
#include <string.h>

// Initial run capacity for a new mapping; doubled on growth.
enum { kSourceMapInitialRuns = 64 };

static const char* source_text_ptr = NULL;
static size_t source_text_len = 0;
static const char* source_file_ptr = NULL;
static const struct SourceMapping* source_map_ptr = NULL;

// ----- Run-length source mappings -----

// Return true when a run whose first byte is at run_loc (covering run_len
// bytes, in advance mode or not) can absorb len more bytes starting at loc.
// A one-byte run or one-byte append fits either mode, so its mode is chosen
// by the neighbor. On success *merged_advance is the combined run's mode.
static bool run_can_extend(const struct SourceMapRun* run, size_t run_len,
                           struct SourceMappingEntry loc, size_t len, bool advance,
                           bool* merged_advance) {
  if (run->filename != loc.filename || run->line != loc.line) {
    return false;
  }
  bool run_may_advance = run->advance || run_len == 1;
  bool run_may_repeat = !run->advance || run_len == 1;
  bool new_may_advance = advance || len == 1;
  bool new_may_repeat = !advance || len == 1;
  if (run_may_advance && new_may_advance && loc.column == run->column + run_len) {
    *merged_advance = true;
    return true;
  }
  if (run_may_repeat && new_may_repeat && loc.column == run->column) {
    *merged_advance = false;
    return true;
  }
  return false;
}

// Append len mapped bytes at the end of map, merging with the last run when
// the mapping continues it.
bool source_mapping_append(struct SourceMapping* map, struct SourceMappingEntry loc,
                           size_t len, bool advance) {
  if (len == 0) return true;
  if (map->run_count > 0) {
    struct SourceMapRun* last = &map->runs[map->run_count - 1];
    bool merged_advance = false;
    if (run_can_extend(last, map->length - last->start, loc, len, advance, &merged_advance)) {
      last->advance = merged_advance;
      map->length += len;
      return true;
    }
  }
  if (map->run_count == map->run_cap) {
    size_t new_cap = map->run_cap == 0 ? kSourceMapInitialRuns : map->run_cap * 2;
    struct SourceMapRun* grown = realloc(map->runs, new_cap * sizeof(*grown));
    if (grown == NULL) return false;
    map->runs = grown;
    map->run_cap = new_cap;
  }
  struct SourceMapRun* run = &map->runs[map->run_count++];
  run->start = map->length;
  run->filename = loc.filename;
  run->line = loc.line;
  run->column = loc.column;
  run->advance = advance || len == 1;
  map->length += len;
  return true;
}

// Return the index of the run containing offset (binary search on start).
static size_t find_run(const struct SourceMapping* map, size_t offset) {
  size_t lo = 0;
  size_t hi = map->run_count; // invariant: runs[lo].start <= offset < runs[hi].start
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (map->runs[mid].start <= offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// Return the coordinate of byte offset within run.
static struct SourceMappingEntry run_entry_at(const struct SourceMapRun* run, size_t offset) {
  struct SourceMappingEntry entry = {run->filename, run->line, run->column};
  if (run->advance) {
    entry.column += offset - run->start;
  }
  return entry;
}

// Copy the mapping for src[offset, offset + len) run by run onto dst.
bool source_mapping_append_range(struct SourceMapping* dst, const struct SourceMapping* src,
                                 size_t offset, size_t len) {
  if (len == 0) return true;
  size_t end = offset + len;
  size_t index = find_run(src, offset);
  while (offset < end) {
    const struct SourceMapRun* run = &src->runs[index];
    size_t run_end = index + 1 < src->run_count ? src->runs[index + 1].start : src->length;
    size_t piece_end = run_end < end ? run_end : end;
    if (!source_mapping_append(dst, run_entry_at(run, offset), piece_end - offset, run->advance)) {
      return false;
    }
    offset = piece_end;
    index++;
  }
  return true;
}

// Return the coordinate of preprocessed byte offset.
struct SourceMappingEntry source_mapping_lookup(const struct SourceMapping* map, size_t offset) {
  return run_entry_at(&map->runs[find_run(map, offset)], offset);
}

// Trim the run array to its final size. Failure to shrink is harmless.
void source_mapping_shrink(struct SourceMapping* map) {
  if (map->run_count == 0 || map->run_count == map->run_cap) return;
  struct SourceMapRun* shrunk = realloc(map->runs, map->run_count * sizeof(*shrunk));
  if (shrunk != NULL) {
    map->runs = shrunk;
    map->run_cap = map->run_count;
  }
}

// Free a mapping's runs and reset it to empty.
void source_mapping_free(struct SourceMapping* map) {
  free(map->runs);
  map->runs = NULL;
  map->run_count = 0;
  map->run_cap = 0;
  map->length = 0;
}

// ----- Active diagnostic context -----

// Install an unmapped source buffer as the active diagnostic context.
void set_source_context(const char* filename, const char* text) {
  set_source_context_with_map(filename, text, NULL);
}

// Install source text and its optional preprocessed-to-original coordinate map.
// The text length is cached because every location lookup clamps against it.
void set_source_context_with_map(const char* filename, const char* text, const struct SourceMapping* map) {
  source_file_ptr = filename;
  source_text_ptr = text;
  source_text_len = text != NULL ? strlen(text) : 0;
  source_map_ptr = map;
}

// Return the filename associated with the current source buffer.
const char* source_filename(void) {
  return source_file_ptr ? source_file_ptr : "<input>";
}

// Find the filename associated with a pointer into the source buffer.
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
  const char* mapped = source_mapping_lookup(source_map_ptr, offset).filename;
  return mapped != NULL ? mapped : source_filename();
}

// Return the complete preprocessed source text.
const char* source_text(void) {
  return source_text_ptr;
}

// Return the one-past-end pointer for the source text.
const char* source_text_end(void) {
  if (source_text_ptr == NULL) return NULL;
  return source_text_ptr + source_text_len;
}

// Compute the line and column for a pointer into the source text.
struct SourceLocation source_location_from_ptr(const char* ptr) {
  struct SourceLocation loc = {0, 0, 0};
  if (source_text_ptr == NULL || ptr == NULL) return loc;

  const char* end = source_text_end();
  if (end != NULL && ptr > end) ptr = end;
  if (ptr < source_text_ptr) return loc;

  if (source_map_ptr != NULL && source_map_ptr->runs != NULL && source_map_ptr->length > 0) {
    size_t offset = (size_t)(ptr - source_text_ptr);
    if (offset >= source_map_ptr->length) {
      offset = source_map_ptr->length - 1;
    }
    struct SourceMappingEntry entry = source_mapping_lookup(source_map_ptr, offset);
    loc.line = entry.line;
    loc.column = entry.column;
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
