#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "preprocessor.h"

// Simple growable output buffer used during preprocessing stages.
struct Buffer {
  char* data;
  size_t len;
  size_t cap;
};

// Object-like macro definition list (no function-style macros).
struct Macro {
  char* name;
  size_t name_len;
  char* value;
  size_t value_len;
  struct Macro* next;
};

// Track nested conditional state and the currently active output region.
struct IfState {
  bool parent_active;
  bool condition_true;
  bool in_else;
};

struct IfStack {
  struct IfState* items;
  size_t count;
  size_t cap;
  bool current_active;
};

static void preprocessor_error_at(const char* filename, size_t line_no, const char* fmt, ...) {
  fprintf(stderr, "Preprocessor error at %s:%zu: ", filename, line_no);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Initialize a buffer with the requested capacity.
static bool buffer_init(struct Buffer* buf, size_t cap) {
  buf->data = malloc(cap);
  if (buf->data == NULL) return false;
  buf->len = 0;
  buf->cap = cap;
  return true;
}

// Ensure the buffer can append add bytes plus a trailing NUL.
static bool buffer_reserve(struct Buffer* buf, size_t add) {
  if (buf->len + add + 1 <= buf->cap) return true;
  size_t new_cap = buf->cap == 0 ? 64 : buf->cap;
  while (new_cap < buf->len + add + 1) new_cap *= 2;
  char* next = realloc(buf->data, new_cap);
  if (next == NULL) return false;
  buf->data = next;
  buf->cap = new_cap;
  return true;
}

// Append a single character to the buffer.
static bool buffer_append_char(struct Buffer* buf, char c) {
  if (!buffer_reserve(buf, 1)) return false;
  buf->data[buf->len++] = c;
  return true;
}

// Append a string slice to the buffer.
static bool buffer_append_str(struct Buffer* buf, const char* s, size_t len) {
  if (!buffer_reserve(buf, len)) return false;
  memcpy(buf->data + buf->len, s, len);
  buf->len += len;
  return true;
}

// NUL-terminate the buffer content.
static bool buffer_finish(struct Buffer* buf) {
  if (!buffer_reserve(buf, 0)) return false;
  buf->data[buf->len] = '\0';
  return true;
}

// Identifier rules used by macro parsing/expansion.
static bool is_ident_start(char c) {
  return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

// Look up a macro by name in the linked list.
static struct Macro* macro_find(struct Macro* macros, const char* name, size_t len) {
  for (struct Macro* macro = macros; macro != NULL; macro = macro->next) {
    if (macro->name_len == len && strncmp(macro->name, name, len) == 0) {
      return macro;
    }
  }
  return NULL;
}

// Define or replace an object-like macro with a raw string value.
static bool macro_define(struct Macro** macros, const char* name, size_t name_len, const char* value, size_t value_len) {
  struct Macro* existing = macro_find(*macros, name, name_len);
  char* value_copy = malloc(value_len + 1);
  if (value_copy == NULL) return false;
  if (value_len > 0) memcpy(value_copy, value, value_len);
  value_copy[value_len] = '\0';

  if (existing != NULL) {
    free(existing->value);
    existing->value = value_copy;
    existing->value_len = value_len;
    return true;
  }

  struct Macro* macro = malloc(sizeof(struct Macro));
  if (macro == NULL) {
    free(value_copy);
    return false;
  }

  char* name_copy = malloc(name_len + 1);
  if (name_copy == NULL) {
    free(value_copy);
    free(macro);
    return false;
  }
  memcpy(name_copy, name, name_len);
  name_copy[name_len] = '\0';

  macro->name = name_copy;
  macro->name_len = name_len;
  macro->value = value_copy;
  macro->value_len = value_len;
  macro->next = *macros;
  *macros = macro;
  return true;
}

// Free all macro storage.
static void destroy_macros(struct Macro* macros) {
  while (macros != NULL) {
    struct Macro* next = macros->next;
    free(macros->name);
    free(macros->value);
    free(macros);
    macros = next;
  }
}

// Push a new conditional state.
static bool ifstack_push(struct IfStack* stack, bool condition_true) {
  if (stack->count == stack->cap) {
    size_t new_cap = stack->cap == 0 ? 8 : stack->cap * 2;
    struct IfState* next = realloc(stack->items, new_cap * sizeof(struct IfState));
    if (next == NULL) return false;
    stack->items = next;
    stack->cap = new_cap;
  }

  struct IfState state = {stack->current_active, condition_true, false};
  stack->items[stack->count++] = state;
  stack->current_active = state.parent_active && state.condition_true;
  return true;
}

// Toggle to the #else branch of the current conditional.
static bool ifstack_else(struct IfStack* stack) {
  if (stack->count == 0) return false;
  struct IfState* state = &stack->items[stack->count - 1];
  if (state->in_else) return false;
  state->in_else = true;
  stack->current_active = state->parent_active && !state->condition_true;
  return true;
}

// Pop the current conditional state.
static bool ifstack_pop(struct IfStack* stack) {
  if (stack->count == 0) return false;
  stack->count--;
  if (stack->count == 0) {
    stack->current_active = true;
  } else {
    struct IfState* state = &stack->items[stack->count - 1];
    stack->current_active = state->parent_active && (state->in_else ? !state->condition_true : state->condition_true);
  }
  return true;
}

// First pass: remove // and /* */ comments while preserving strings/chars.
static char* strip_comments(char const* prog) {
  struct Buffer out;
  if (!buffer_init(&out, 64)) {
    fprintf(stderr, "Preprocessor memory error\n");
    return NULL;
  }

  size_t prog_index = 0;
  bool in_string = false;
  bool in_char = false;
  bool escape = false;

  while (prog[prog_index] != 0) {
    if (!in_string && !in_char) {
      // remove single line // comments
      if (prog[prog_index] == '/' && prog[prog_index + 1] == '/') {
        if (!buffer_append_char(&out, ' ')) goto fail;
        prog_index += 2;
        while (prog[prog_index] != '\0' && prog[prog_index] != '\n') {
          prog_index++;
        }
        if (prog[prog_index] == '\n') {
          if (!buffer_append_char(&out, '\n')) goto fail;
          prog_index++;
        }
        continue;
      }

      // remove multi line /* */ comments
      if (prog[prog_index] == '/' && prog[prog_index + 1] == '*') {
        if (!buffer_append_char(&out, ' ')) goto fail;
        prog_index += 2;
        while (prog[prog_index] != '\0') {
          if (prog[prog_index] == '*' && prog[prog_index + 1] == '/') {
            prog_index += 2;
            break;
          }
          prog_index++;
        }
        continue;
      }

      if (prog[prog_index] == '"') {
        in_string = true;
      } else if (prog[prog_index] == '\'') {
        in_char = true;
      }
    } else {
      if (escape) {
        escape = false;
      } else if (prog[prog_index] == '\\') {
        escape = true;
      } else if (in_string && prog[prog_index] == '"') {
        in_string = false;
      } else if (in_char && prog[prog_index] == '\'') {
        in_char = false;
      }
    }

    if (!buffer_append_char(&out, prog[prog_index])) goto fail;
    prog_index++;
  }

  if (!buffer_finish(&out)) goto fail;
  return out.data;

fail:
  fprintf(stderr, "Preprocessor memory error\n");
  free(out.data);
  return NULL;
}

// Read an entire file into a newly allocated buffer.
static char* read_file(const char* path, const char* include_from, size_t line_no) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    preprocessor_error_at(include_from, line_no,
                          "failed to open include file: %s", path);
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    preprocessor_error_at(include_from, line_no,
                          "failed to seek include file: %s", path);
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    preprocessor_error_at(include_from, line_no,
                          "failed to size include file: %s", path);
    fclose(file);
    return NULL;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    preprocessor_error_at(include_from, line_no,
                          "failed to seek include file: %s", path);
    fclose(file);
    return NULL;
  }

  char* buffer = malloc((size_t)size + 1);
  if (buffer == NULL) {
    preprocessor_error_at(include_from, line_no,
                          "memory error while reading include file: %s", path);
    fclose(file);
    return NULL;
  }

  size_t read = fread(buffer, 1, (size_t)size, file);
  if (read != (size_t)size) {
    preprocessor_error_at(include_from, line_no,
                          "failed to read include file: %s", path);
    free(buffer);
    fclose(file);
    return NULL;
  }
  buffer[size] = '\0';
  fclose(file);
  return buffer;
}

// Heap copy for paths and include names.
static char* copy_string(const char* src) {
  size_t len = strlen(src);
  char* out = malloc(len + 1);
  if (out == NULL) return NULL;
  memcpy(out, src, len);
  out[len] = '\0';
  return out;
}

// Resolve include paths relative to the current file (no <...> support).
static char* resolve_include_path(const char* current_file, const char* include_name) {
  if (include_name[0] == '/') return copy_string(include_name);

  const char* slash = strrchr(current_file, '/');
  if (slash == NULL) return copy_string(include_name);

  size_t dir_len = (size_t)(slash - current_file);
  size_t inc_len = strlen(include_name);
  char* out = malloc(dir_len + 1 + inc_len + 1);
  if (out == NULL) return NULL;
  memcpy(out, current_file, dir_len);
  out[dir_len] = '/';
  memcpy(out + dir_len + 1, include_name, inc_len);
  out[dir_len + 1 + inc_len] = '\0';
  return out;
}

// Validate -D and #define names.
static bool is_valid_macro_name(const char* name, size_t len) {
  if (len == 0 || !is_ident_start(name[0])) return false;
  for (size_t i = 1; i < len; ++i) {
    if (!is_ident_char(name[i])) return false;
  }
  return true;
}

// Apply -DNAME or -DNAME=value from the command line.
static bool apply_cli_defines(struct Macro** macros, int num_defines, const char* const* defines) {
  for (int i = 0; i < num_defines; ++i) {
    const char* def = defines[i];
    if (def == NULL || def[0] == '\0') {
      fprintf(stderr, "Invalid -D definition\n");
      return false;
    }

    const char* eq = strchr(def, '=');
    size_t name_len = eq == NULL ? strlen(def) : (size_t)(eq - def);
    if (!is_valid_macro_name(def, name_len)) {
      fprintf(stderr, "Invalid -D name: %.*s\n", (int)name_len, def);
      return false;
    }

    const char* value = eq == NULL ? "1" : eq + 1;
    size_t value_len = eq == NULL ? 1 : strlen(eq + 1);
    if (!macro_define(macros, def, name_len, value, value_len)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
  }
  return true;
}

// Expand macros in a single line, skipping strings and character literals.
static bool expand_macros_in_line(const char* line, size_t len, struct Macro* macros, struct Buffer* out) {
  bool in_string = false;
  bool in_char = false;
  bool escape = false;

  for (size_t i = 0; i < len; ) {
    char c = line[i];

    if (in_string) {
      if (!buffer_append_char(out, c)) return false;
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      i++;
      continue;
    }

    if (in_char) {
      if (!buffer_append_char(out, c)) return false;
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '\'') {
        in_char = false;
      }
      i++;
      continue;
    }

    if (c == '"') {
      in_string = true;
      if (!buffer_append_char(out, c)) return false;
      i++;
      continue;
    }
    if (c == '\'') {
      in_char = true;
      if (!buffer_append_char(out, c)) return false;
      i++;
      continue;
    }

    if (is_ident_start(c)) {
      size_t start = i;
      i++;
      while (i < len && is_ident_char(line[i])) i++;
      struct Macro* macro = macro_find(macros, line + start, i - start);
      if (macro != NULL) {
        if (!buffer_append_str(out, macro->value, macro->value_len)) return false;
      } else {
        if (!buffer_append_str(out, line + start, i - start)) return false;
      }
      continue;
    }

    if (!buffer_append_char(out, c)) return false;
    i++;
  }

  return true;
}

// Parse a #define line (object-like only) and store it in the macro list.
static bool parse_define_line(const char* line, const char* line_end,
                              const char* filename, size_t line_no,
                              struct Macro** macros) {
  const char* p = line;
  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (p >= line_end || !is_ident_start(*p)) {
    preprocessor_error_at(filename, line_no, "invalid #define directive");
    return false;
  }

  const char* name_start = p;
  p++;
  while (p < line_end && is_ident_char(*p)) p++;
  size_t name_len = (size_t)(p - name_start);

  while (p < line_end && isspace((unsigned char)*p)) p++;
  const char* value_start = p;
  const char* value_end = line_end;
  while (value_end > value_start && isspace((unsigned char)*(value_end - 1))) value_end--;

  if (!macro_define(macros, name_start, name_len, value_start, (size_t)(value_end - value_start))) {
    preprocessor_error_at(filename, line_no,
                          "memory error while defining macro");
    return false;
  }
  return true;
}

// Main preprocessor for a single source buffer. Shares macro list with includes.
static char* preprocess_buffer(const char* prog, const char* filename, struct Macro** macros);

// Handle #include "path" by preprocessing the included file in place.
static bool handle_include_line(const char* line, const char* line_end, const char* filename,
                                size_t line_no, struct Macro** macros, struct Buffer* out,
                                bool add_newline) {
  const char* p = line;
  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (p >= line_end || *p != '"') {
    preprocessor_error_at(filename, line_no, "invalid #include directive");
    return false;
  }
  p++;
  const char* name_start = p;
  while (p < line_end && *p != '"') p++;
  if (p >= line_end) {
    preprocessor_error_at(filename, line_no, "invalid #include directive");
    return false;
  }
  size_t name_len = (size_t)(p - name_start);

  char* include_name = malloc(name_len + 1);
  if (include_name == NULL) {
    preprocessor_error_at(filename, line_no,
                          "memory error while parsing include directive");
    return false;
  }
  memcpy(include_name, name_start, name_len);
  include_name[name_len] = '\0';

  char* include_path = resolve_include_path(filename, include_name);
  free(include_name);
  if (include_path == NULL) {
    preprocessor_error_at(filename, line_no,
                          "memory error while resolving include path");
    return false;
  }

  char* include_source = read_file(include_path, filename, line_no);
  if (include_source == NULL) {
    free(include_path);
    return false;
  }

  char* include_output = preprocess_buffer(include_source, include_path, macros);
  free(include_source);
  free(include_path);
  if (include_output == NULL) return false;

  size_t include_len = strlen(include_output);
  bool ok = buffer_append_str(out, include_output, include_len);
  if (ok && add_newline && (include_len == 0 || include_output[include_len - 1] != '\n')) {
    ok = buffer_append_char(out, '\n');
  }
  free(include_output);
  return ok;
}

// Basic validation for #ifdef/#ifndef names with parsed output.
static bool parse_ifdef_name(const char* line, const char* line_end,
                             const char* filename, size_t line_no,
                             const char* directive,
                             const char** name_start, size_t* name_len) {
  const char* p = line;
  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (p >= line_end || !is_ident_start(*p)) {
    preprocessor_error_at(filename, line_no, "invalid #%s directive", directive);
    return false;
  }
  const char* start = p;
  p++;
  while (p < line_end && is_ident_char(*p)) p++;
  *name_start = start;
  *name_len = (size_t)(p - start);
  return true;
}

// Process a single preprocessor directive line.
static bool preprocess_directive(
    const char* line_start,
    const char* line_end,
    bool has_newline,
    size_t line_no,
    const char* filename,
    struct Macro** macros,
    struct Buffer* out,
    struct IfStack* if_stack) {
  const char* p = line_start;
  while (p < line_end && isspace((unsigned char)*p)) p++;
  const char* word_start = p;
  while (p < line_end && isalpha((unsigned char)*p)) p++;
  size_t word_len = (size_t)(p - word_start);

  if (word_len == 0) {
    preprocessor_error_at(filename, line_no, "invalid preprocessor directive");
    return false;
  }

  bool is_active = if_stack->current_active;

  if (word_len == 7 && strncmp(word_start, "include", 7) == 0) {
    if (!is_active) return true;
    return handle_include_line(p, line_end, filename, line_no, macros, out, has_newline);
  }

  if (word_len == 6 && strncmp(word_start, "define", 6) == 0) {
    if (!is_active) return true;
    return parse_define_line(p, line_end, filename, line_no, macros);
  }

  if (word_len == 5 && strncmp(word_start, "ifdef", 5) == 0) {
    const char* name_start = NULL;
    size_t name_len = 0;
    if (!parse_ifdef_name(p, line_end, filename, line_no, "ifdef", &name_start, &name_len)) {
      return false;
    }
    bool condition_true = false;
    if (if_stack->current_active) {
      condition_true = (macro_find(*macros, name_start, name_len) != NULL);
    }
    if (!ifstack_push(if_stack, condition_true)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
    return true;
  }

  if (word_len == 6 && strncmp(word_start, "ifndef", 6) == 0) {
    const char* name_start = NULL;
    size_t name_len = 0;
    if (!parse_ifdef_name(p, line_end, filename, line_no, "ifndef", &name_start, &name_len)) {
      return false;
    }
    bool condition_true = false;
    if (if_stack->current_active) {
      condition_true = (macro_find(*macros, name_start, name_len) == NULL);
    }
    if (!ifstack_push(if_stack, condition_true)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
    return true;
  }

  if (word_len == 4 && strncmp(word_start, "else", 4) == 0) {
    if (!ifstack_else(if_stack)) {
      preprocessor_error_at(filename, line_no, "unexpected #else");
      return false;
    }
    return true;
  }

  if (word_len == 5 && strncmp(word_start, "endif", 5) == 0) {
    if (!ifstack_pop(if_stack)) {
      preprocessor_error_at(filename, line_no, "unexpected #endif");
      return false;
    }
    return true;
  }

  preprocessor_error_at(filename, line_no, "unknown preprocessor directive");
  return false;
}

// Pipeline: strip comments, apply directives, and expand macros line-by-line.
static char* preprocess_buffer(const char* prog, const char* filename, struct Macro** macros) {
  char* no_comments = strip_comments(prog);
  if (no_comments == NULL) return NULL;

  struct Buffer out;
  size_t initial_cap = strlen(no_comments) + 1;
  if (initial_cap < 64) initial_cap = 64;
  if (!buffer_init(&out, initial_cap)) {
    fprintf(stderr, "Preprocessor memory error\n");
    free(no_comments);
    return NULL;
  }

  // Track nesting and whether we're currently emitting active regions.
  struct IfStack if_stack;
  if_stack.items = NULL;
  if_stack.count = 0;
  if_stack.cap = 0;
  if_stack.current_active = true;

  const char* cursor = no_comments;
  size_t line_no = 1;
  while (*cursor != '\0') {
    const char* line_start = cursor;
    while (*cursor != '\0' && *cursor != '\n') cursor++;
    const char* line_end = cursor;
    bool has_newline = (*cursor == '\n');
    if (has_newline) cursor++;

    const char* p = line_start;
    while (p < line_end && isspace((unsigned char)*p)) p++;
    // Directive lines are recognized even when indented.
    if (p < line_end && *p == '#') {
      if (!preprocess_directive(p + 1, line_end, has_newline, line_no,
                                filename, macros, &out, &if_stack)) {
        free(no_comments);
        free(out.data);
        free(if_stack.items);
        return NULL;
      }
      if (has_newline) line_no++;
      continue;
    }

    // Only emit non-directive lines from active regions.
    if (if_stack.current_active) {
      if (!expand_macros_in_line(line_start, (size_t)(line_end - line_start), *macros, &out)) {
        fprintf(stderr, "Preprocessor memory error\n");
        free(no_comments);
        free(out.data);
        free(if_stack.items);
        return NULL;
      }
      if (has_newline && !buffer_append_char(&out, '\n')) {
        fprintf(stderr, "Preprocessor memory error\n");
        free(no_comments);
        free(out.data);
        free(if_stack.items);
        return NULL;
      }
    }
    if (has_newline) line_no++;
  }

  // Unterminated #ifdef/#ifndef should be reported as an error.
  if (if_stack.count != 0) {
    preprocessor_error_at(filename, line_no,
                          "unterminated #ifdef/#ifndef block (reached end of file)");
    free(no_comments);
    free(out.data);
    free(if_stack.items);
    return NULL;
  }

  free(no_comments);
  free(if_stack.items);
  if (!buffer_finish(&out)) {
    fprintf(stderr, "Preprocessor memory error\n");
    free(out.data);
    return NULL;
  }
  return out.data;
}

// Public entrypoint: apply CLI defines, then preprocess the buffer.
char* preprocess(char const* prog, const char* filename, int num_defines, const char* const* defines) {
  struct Macro* macros = NULL;
  if (!apply_cli_defines(&macros, num_defines, defines)) {
    destroy_macros(macros);
    return NULL;
  }

  char* out = preprocess_buffer(prog, filename, &macros);
  destroy_macros(macros);
  return out;
}
