#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>

#include "source_location.h"

// Purpose: Own interned file name storage for source mappings.
// Inputs/Outputs: Managed by the preprocessor; freed by destroy_preprocess_result.
// Invariants/Assumptions: names entries are heap-allocated NUL-terminated strings.
struct FileTable {
  char** names;
  size_t count;
  size_t cap;
};

// Purpose: Bundle preprocessed output with source mapping metadata.
// Inputs/Outputs: text and map are allocated by preprocess and freed by destroy_preprocess_result.
// Invariants/Assumptions: map.length matches strlen(text); file_table owns filenames used by map.
struct PreprocessResult {
  char* text;
  struct SourceMapping map;
  struct FileTable file_table;
};

// Purpose: Preprocess a source buffer (comments, directives, object-like macros).
// Inputs: prog is the source buffer; filename tags diagnostics; defines mirrors -D flags.
// Outputs: Returns true on success and fills result; false on error.
// Invariants/Assumptions: Only quoted includes and object-like macros are supported.
bool preprocess(char const* prog, const char* filename, int num_defines,
                const char* const* defines, struct PreprocessResult* result);

// Purpose: Free all storage owned by a PreprocessResult.
// Inputs: result was filled by preprocess.
// Outputs: Releases heap allocations and zeroes pointers/counts.
// Invariants/Assumptions: Safe to call with partially initialized results.
void destroy_preprocess_result(struct PreprocessResult* result);

#endif
