#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>

#include "source_location.h"

// Own interned file name storage for source mappings.
// names entries are heap-allocated NUL-terminated strings.
struct FileTable {
  char** names;
  size_t count;
  size_t cap;
};

// Bundle preprocessed output with source mapping metadata.
struct PreprocessResult {
  char* text;
  struct SourceMapping map;
  struct FileTable file_table;
};

// Preprocess a source buffer (comments, directives, object-like macros).
// Returns true on success and fills result; false on error.
bool preprocess(char const* prog, const char* filename, int num_defines,
                const char* const* defines, struct PreprocessResult* result);

// Free all storage owned by a PreprocessResult.
// Safe to call with partially initialized results.
void destroy_preprocess_result(struct PreprocessResult* result);

#endif
