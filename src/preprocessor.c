#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>

#include "preprocessor.h"
#include "arena.h"

// Implement a minimal preprocessor with comment stripping, object-like and
// function-like macro expansion, and basic conditional/include handling.
// Supported directives: #include "path", #define, #undef, #if (defined terms
// joined by &&), #ifdef, #ifndef, #else, #endif, and #pragma (ignored). Not
// supported: #elif and <...> includes. Backslash-newline splices and comments
// are removed first, so a directive may continue across physical lines.
//
// Macro expansion follows C11 6.10.3 using the hide-set formulation of the
// rescanning rules: every token produced by expansion records the macros that
// produced it, and an identifier is never expanded by a macro in its own hide
// set. Supported: parameters, # stringizing, ## pasting with placemarkers,
// variadic macros with __VA_ARGS__, and rescanning that may consume tokens
// following an expansion (f(1)(2) where f expands to a function-like name).
// Extension: GNU ", ## __VA_ARGS__" deletes the comma when the variable
// arguments are empty (C11 would instead fail to paste "," with the first token).

// Track a growable output buffer and its run-length source mapping during
// preprocessing. map.length always equals len.
struct Buffer {
  char* data;
  struct SourceMapping map;
  size_t len;
  size_t cap;
};

// Classify preprocessing tokens (C11 6.4). Character constants and string
// literals share PP_LITERAL; anything that is not an identifier, pp-number, or
// literal is a PP_PUNCT, lexed with maximal munch over the C punctuators.
enum PPTokenKind {
  PP_IDENT,
  PP_NUMBER,
  PP_LITERAL,
  PP_PUNCT,
};

// One token of a macro replacement list, lexed once when the macro is defined.
// Its text is value[offset, offset + len) of the owning Macro.
struct MacroBodyToken {
  enum PPTokenKind kind;
  size_t offset;
  size_t len;
  bool space_before;
  // Index of the parameter this identifier names, or -1. In a variadic macro
  // __VA_ARGS__ uses the last slot, param_count - 1.
  int param;
};

// Own one macro definition and link it into the preprocessor table.
// Owned by the preprocessor and freed at teardown.
struct Macro {
  char* name;
  size_t name_len;
  char* value;  // trimmed replacement text; body tokens point into it
  size_t value_len;
  struct MacroBodyToken* body;
  size_t body_len;
  bool function_like;
  bool variadic;       // parameter list ends with "..."
  size_t param_count;  // named parameters, plus one __VA_ARGS__ slot if variadic
  struct Macro* next;
};

// A parameter name slice from a #define line, used only while lexing the body.
struct MacroParam {
  const char* name;
  size_t len;
};

// Identify builtin macro names with special expansion behavior.
static const char kBuiltinFileMacro[] = "__FILE__";
static const char kBuiltinLineMacro[] = "__LINE__";
static const size_t kBuiltinFileMacroLen = sizeof(kBuiltinFileMacro) - 1;
static const size_t kBuiltinLineMacroLen = sizeof(kBuiltinLineMacro) - 1;
enum { LINE_NUMBER_BUFFER_CAP = (int)(sizeof(size_t) * CHAR_BIT + 1) };
enum { FILE_TABLE_INITIAL_CAP = 8 };
// Scratch arena block size for macro expansion; a typical invocation fits in one block.
enum { PREPROCESS_SCRATCH_BLOCK_SIZE = 4096 };

// Store preprocessed output and source mapping together.
struct PreprocessOutput {
  char* text;
  struct SourceMapping map;
};

// Forward declarations for file table helpers used before definition.
// Filename storage is owned by the file table.
static void file_table_init(struct FileTable* table);
static const char* file_table_intern(struct FileTable* table, const char* name);
static void file_table_destroy(struct FileTable* table);

// Track one level of #ifdef/#ifndef state.
struct IfState {
  bool parent_active;
  bool condition_true;
  bool in_else;
};

// Maintain a stack of nested #if states with current activity.
struct IfStack {
  struct IfState* items;
  size_t count;
  size_t cap;
  bool current_active;
};

// Report a preprocessor error with filename and line context.
static void preprocessor_error_at(const char* filename, size_t line_no, const char* fmt, ...) {
  fprintf(stderr, "Preprocessor error at %s:%zu: ", filename, line_no);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Allocate an empty output buffer with an empty source mapping.
// Returns false if the allocation fails.
static bool buffer_init(struct Buffer* buf, size_t cap) {
  buf->data = malloc(cap);
  if (buf->data == NULL) return false;
  buf->map.runs = NULL;
  buf->map.run_count = 0;
  buf->map.run_cap = 0;
  buf->map.length = 0;
  buf->len = 0;
  buf->cap = cap;
  return true;
}

// Free the buffer's text and source mapping.
static void buffer_free(struct Buffer* buf) {
  free(buf->data);
  buf->data = NULL;
  source_mapping_free(&buf->map);
  buf->len = 0;
  buf->cap = 0;
}

// Ensure the buffer can append add bytes plus a trailing NUL.
// Returns true on success and grows the buffer if needed.
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

// Append a single character and mapping entry to the buffer.
// Returns true on success and increments buf->len.
// buffer_reserve must succeed for append.
static bool buffer_append_char(struct Buffer* buf, char c, struct SourceMappingEntry loc) {
  if (!buffer_reserve(buf, 1)) return false;
  if (!source_mapping_append(&buf->map, loc, 1, true)) return false;
  buf->data[buf->len++] = c;
  return true;
}

// Append a string slice with a single source location.
// Returns true on success and increments buf->len.
// s may be non-NUL-terminated; loc applies to all bytes.
static bool buffer_append_str_with_loc(struct Buffer* buf, const char* s, size_t len,
                                       struct SourceMappingEntry loc) {
  if (!buffer_reserve(buf, len)) return false;
  if (!source_mapping_append(&buf->map, loc, len, false)) return false;
  memcpy(buf->data + buf->len, s, len);
  buf->len += len;
  return true;
}

// Append src bytes [offset, offset + len) together with their source mapping.
// Returns true on success and increments buf->len.
static bool buffer_append_range(struct Buffer* buf, const char* src_text,
                                const struct SourceMapping* src_map,
                                size_t offset, size_t len) {
  if (!buffer_reserve(buf, len)) return false;
  if (!source_mapping_append_range(&buf->map, src_map, offset, len)) return false;
  memcpy(buf->data + buf->len, src_text + offset, len);
  buf->len += len;
  return true;
}

// NUL-terminate the buffer content.
// Returns true on success and writes a trailing '\0'.
static bool buffer_finish(struct Buffer* buf) {
  if (!buffer_reserve(buf, 0)) return false;
  buf->data[buf->len] = '\0';
  return true;
}

// Test whether a character can start an identifier.
// Returns true when c can begin an identifier.
static bool is_ident_start(char c) {
  return isalpha((unsigned char)c) || c == '_';
}

// Test whether a character can appear inside an identifier.
// Returns true when c is a valid identifier character.
static bool is_ident_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

// Check if the identifier matches a reserved builtin macro name.
// Returns true when the name is a builtin macro.
// name may be non-NUL-terminated.
static bool is_builtin_macro_name(const char* name, size_t len) {
  if (len == kBuiltinFileMacroLen && strncmp(name, kBuiltinFileMacro, kBuiltinFileMacroLen) == 0) {
    return true;
  }
  if (len == kBuiltinLineMacroLen && strncmp(name, kBuiltinLineMacro, kBuiltinLineMacroLen) == 0) {
    return true;
  }
  return false;
}

// Look up a macro by name in the linked list.
// Returns a pointer to the macro or NULL if not found.
// Macro names are stored as NUL-terminated copies.
static struct Macro* macro_find(struct Macro* macros, const char* name, size_t len) {
  for (struct Macro* macro = macros; macro != NULL; macro = macro->next) {
    if (macro->name_len == len && strncmp(macro->name, name, len) == 0) {
      return macro;
    }
  }
  return NULL;
}

// Check whether a macro name is defined either as builtin or user macro.
// Returns true when a macro is considered defined.
static bool is_macro_defined(struct Macro* macros, const char* name, size_t len) {
  if (is_builtin_macro_name(name, len)) return true;
  return macro_find(macros, name, len) != NULL;
}

// Whitespace that separates preprocessing tokens. Newlines count because an
// invocation's argument list may continue onto following source lines.
static bool is_pp_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Multi-character C punctuators, longest first so the scan is maximal munch.
// "##" is included because it is the token-pasting operator in macro bodies.
static const char* const kMultiCharPunctuators[] = {
  "...", "<<=", ">>=",
  "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
  "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##",
};

// Measure the preprocessing token at text[0] (avail > 0, not whitespace).
// A literal ends at its closing quote; an unterminated literal stops before a
// newline (C literals cannot contain one) and is diagnosed later by the lexer.
static size_t pp_token_length(const char* text, size_t avail, enum PPTokenKind* kind) {
  char c = text[0];
  if (is_ident_start(c)) {
    size_t i = 1;
    while (i < avail && is_ident_char(text[i])) i++;
    *kind = PP_IDENT;
    return i;
  }

  // pp-number (C11 6.4.8): a digit or '.' digit, then identifier characters,
  // '.', and signs that follow an exponent letter. It deliberately swallows
  // suffixes and hex digits so "0x1F" is never scanned as identifier "x1F".
  if (isdigit((unsigned char)c) ||
      (c == '.' && avail > 1 && isdigit((unsigned char)text[1]))) {
    size_t i = 1;
    while (i < avail) {
      char d = text[i];
      char prev = text[i - 1];
      bool exponent_sign = (d == '+' || d == '-') &&
                           (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P');
      if (!exponent_sign && !is_ident_char(d) && d != '.') break;
      i++;
    }
    *kind = PP_NUMBER;
    return i;
  }

  if (c == '"' || c == '\'') {
    size_t i = 1;
    bool escape = false;
    while (i < avail && text[i] != '\n') {
      char d = text[i++];
      if (escape) {
        escape = false;
      } else if (d == '\\') {
        escape = true;
      } else if (d == c) {
        break;
      }
    }
    *kind = PP_LITERAL;
    return i;
  }

  *kind = PP_PUNCT;
  size_t count = sizeof(kMultiCharPunctuators) / sizeof(kMultiCharPunctuators[0]);
  for (size_t i = 0; i < count; ++i) {
    size_t len = strlen(kMultiCharPunctuators[i]);
    if (len <= avail && memcmp(text, kMultiCharPunctuators[i], len) == 0) return len;
  }
  return 1;
}

// Compare a token's text against a punctuator spelling.
static bool pp_text_is_punct(enum PPTokenKind kind, const char* text, size_t len,
                             const char* punct) {
  size_t punct_len = strlen(punct);
  return kind == PP_PUNCT && len == punct_len && memcmp(text, punct, punct_len) == 0;
}

// Test whether body token i of m is the given punctuator.
static bool macro_body_is(const struct Macro* m, size_t i, const char* punct) {
  const struct MacroBodyToken* bt = &m->body[i];
  return pp_text_is_punct(bt->kind, m->value + bt->offset, bt->len, punct);
}

// Lex a replacement list into body tokens, resolving parameter names and
// enforcing the constraints of C11 6.10.3: __VA_ARGS__ only in variadic
// macros, '#' followed by a parameter (function-like only), and '##' at
// neither end. Returns false after reporting an error at filename:line_no.
static bool lex_macro_body(struct Macro* m, const struct MacroParam* params,
                           size_t named_count, const char* filename, size_t line_no) {
  static const char kVaArgs[] = "__VA_ARGS__";
  size_t cap = 0;
  bool space = false;
  m->body = NULL;
  m->body_len = 0;

  for (size_t i = 0; i < m->value_len; ) {
    if (is_pp_space(m->value[i])) {
      space = true;
      i++;
      continue;
    }

    struct MacroBodyToken tok;
    tok.offset = i;
    tok.len = pp_token_length(m->value + i, m->value_len - i, &tok.kind);
    tok.space_before = space;
    tok.param = -1;
    if (tok.kind == PP_IDENT) {
      const char* text = m->value + i;
      if (tok.len == sizeof(kVaArgs) - 1 && memcmp(text, kVaArgs, tok.len) == 0) {
        if (!m->variadic) {
          preprocessor_error_at(filename, line_no,
                                "__VA_ARGS__ can only appear in the replacement list of a "
                                "variadic macro (macro \"%s\" is not declared with \"...\")",
                                m->name);
          return false;
        }
        tok.param = (int)(m->param_count - 1);
      } else {
        for (size_t p = 0; p < named_count; ++p) {
          if (params[p].len == tok.len && memcmp(params[p].name, text, tok.len) == 0) {
            tok.param = (int)p;
            break;
          }
        }
      }
    }

    if (m->body_len == cap) {
      size_t new_cap = cap == 0 ? 8 : cap * 2;
      struct MacroBodyToken* next = realloc(m->body, new_cap * sizeof(*next));
      if (next == NULL) {
        preprocessor_error_at(filename, line_no,
                              "memory error while lexing body of macro \"%s\"", m->name);
        return false;
      }
      m->body = next;
      cap = new_cap;
    }
    m->body[m->body_len++] = tok;
    space = false;
    i += tok.len;
  }

  if (m->body_len > 0 &&
      (macro_body_is(m, 0, "##") || macro_body_is(m, m->body_len - 1, "##"))) {
    preprocessor_error_at(filename, line_no,
                          "'##' cannot appear at either end of the replacement list "
                          "of macro \"%s\"", m->name);
    return false;
  }

  if (m->function_like) {
    for (size_t i = 0; i < m->body_len; ++i) {
      if (macro_body_is(m, i, "#") &&
          (i + 1 == m->body_len || m->body[i + 1].param < 0)) {
        preprocessor_error_at(filename, line_no,
                              "'#' is not followed by a macro parameter in the "
                              "replacement list of macro \"%s\"", m->name);
        return false;
      }
    }
  }
  return true;
}

// Free the storage a macro owns, including the macro itself.
static void macro_free(struct Macro* macro) {
  free(macro->name);
  free(macro->value);
  free(macro->body);
  free(macro);
}

// Define or replace a macro. value is the trimmed replacement text; params
// are the named parameters of a function-like macro, and variadic adds the
// __VA_ARGS__ slot after them. A redefinition replaces the old definition
// without checking that it is identical (C11 6.10.3p2 would require that).
// Returns false after reporting an error at filename:line_no.
static bool macro_define(struct Macro** macros, const char* name, size_t name_len,
                         const char* value, size_t value_len,
                         bool function_like, bool variadic,
                         const struct MacroParam* params, size_t named_count,
                         const char* filename, size_t line_no) {
  struct Macro* macro = calloc(1, sizeof(struct Macro));
  if (macro != NULL) {
    macro->name = malloc(name_len + 1);
    macro->value = malloc(value_len + 1);
  }
  if (macro == NULL || macro->name == NULL || macro->value == NULL) {
    preprocessor_error_at(filename, line_no,
                          "memory error while defining macro \"%.*s\"",
                          (int)name_len, name);
    if (macro != NULL) macro_free(macro);
    return false;
  }
  memcpy(macro->name, name, name_len);
  macro->name[name_len] = '\0';
  macro->name_len = name_len;
  if (value_len > 0) memcpy(macro->value, value, value_len);
  macro->value[value_len] = '\0';
  macro->value_len = value_len;
  macro->function_like = function_like;
  macro->variadic = variadic;
  macro->param_count = named_count + (variadic ? 1 : 0);

  if (!lex_macro_body(macro, params, named_count, filename, line_no)) {
    macro_free(macro);
    return false;
  }

  // Replace in place so the list keeps one entry per name.
  struct Macro* existing = macro_find(*macros, name, name_len);
  if (existing != NULL) {
    struct Macro* next = existing->next;
    free(existing->name);
    free(existing->value);
    free(existing->body);
    *existing = *macro;
    existing->next = next;
    free(macro);
    return true;
  }

  macro->next = *macros;
  *macros = macro;
  return true;
}

// Remove a macro definition if one exists; undefining an undefined name is
// not an error (C11 6.10.3.5p2). Must not run during an expansion, whose hide
// sets point at Macro objects (directives are only processed between spans).
static void macro_undefine(struct Macro** macros, const char* name, size_t name_len) {
  for (struct Macro** link = macros; *link != NULL; link = &(*link)->next) {
    struct Macro* macro = *link;
    if (macro->name_len == name_len && strncmp(macro->name, name, name_len) == 0) {
      *link = macro->next;
      macro_free(macro);
      return;
    }
  }
}

// Free all macro storage owned by the preprocessor.
static void destroy_macros(struct Macro* macros) {
  while (macros != NULL) {
    struct Macro* next = macros->next;
    macro_free(macros);
    macros = next;
  }
}

// Push a new conditional state for #ifdef/#ifndef.
// Returns true on success and updates stack->current_active.
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
// Returns true on success and updates stack->current_active.
// Each conditional may have at most one #else.
static bool ifstack_else(struct IfStack* stack) {
  if (stack->count == 0) return false;
  struct IfState* state = &stack->items[stack->count - 1];
  if (state->in_else) return false;
  state->in_else = true;
  stack->current_active = state->parent_active && !state->condition_true;
  return true;
}

// Pop the current conditional state.
// Returns true on success and updates stack->current_active.
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

// Read position in a raw source buffer during comment stripping. Backslash-
// newline splices (C11 5.1.1.2 phase 2) are skipped transparently, so index
// always rests on a logical character whose original location is line/column.
// hidden_newlines counts physical newlines removed by splices or swallowed by
// block comments since the last newline was written to the output.
struct SourceCursor {
  const char* text;
  size_t index;
  size_t line;
  size_t column;
  size_t hidden_newlines;
};

// Return the length of a line splice at text[i] (a backslash followed by "\n"
// or "\r\n"), or 0. text is NUL-terminated, so the lookahead stays in bounds.
static size_t splice_length(const char* text, size_t i) {
  if (text[i] != '\\') return 0;
  if (text[i + 1] == '\n') return 2;
  if (text[i + 1] == '\r' && text[i + 2] == '\n') return 3;
  return 0;
}

// Skip any splices at the cursor.
static void cursor_skip_splices(struct SourceCursor* cur) {
  size_t len;
  while ((len = splice_length(cur->text, cur->index)) != 0) {
    cur->index += len;
    cur->line++;
    cur->column = 1;
    cur->hidden_newlines++;
  }
}

// Move past the current logical character and any splices after it. The
// cursor must not be at the terminating NUL.
static void cursor_advance(struct SourceCursor* cur) {
  if (cur->text[cur->index] == '\n') {
    cur->line++;
    cur->column = 1;
  } else {
    cur->column++;
  }
  cur->index++;
  // Fast path for the per-byte copy loop: only a backslash can start a splice.
  if (cur->text[cur->index] == '\\') cursor_skip_splices(cur);
}

// Return the logical character after the current one, looking through splices.
// The cursor must not be at the terminating NUL.
static char cursor_peek_next(const struct SourceCursor* cur) {
  size_t i = cur->index + 1;
  size_t len;
  while ((len = splice_length(cur->text, i)) != 0) i += len;
  return cur->text[i];
}

// Write the newlines that splices and block comments removed from the current
// logical line, so later output lines keep their source line numbers.
static bool flush_hidden_newlines(struct SourceCursor* cur, struct Buffer* out,
                                  struct SourceMappingEntry loc) {
  for (; cur->hidden_newlines > 0; cur->hidden_newlines--) {
    if (!buffer_append_char(out, '\n', loc)) return false;
  }
  return true;
}

// Splice lines and replace each comment with one space (C11 5.1.1.2 phases 2
// and 3) while preserving string and character literals. A comment or splice
// that crosses lines therefore joins them into one logical line, which is what
// lets a #define continue past a backslash or a multi-line comment; the
// swallowed newlines are written after the logical line ends.
// prog is a NUL-terminated source buffer of prog_len bytes; filename identifies
// the source. Output never exceeds prog_len (a splice or comment is at least as
// long as what replaces it), so it is reserved once.
// Returns true on success and fills out with comment-stripped text and mappings.
static bool strip_comments(const char* prog, size_t prog_len, const char* filename,
                           struct FileTable* files, struct Buffer* out) {
  const char* interned = file_table_intern(files, filename);
  if (interned == NULL) {
    fprintf(stderr, "Preprocessor memory error\n");
    return false;
  }

  // C11 5.1.1.2p2: a file's final newline must not be preceded by a backslash.
  // Splicing it would silently drop a trailing backslash (undefined behavior,
  // which gcc accepts with a warning); reject it instead.
  size_t trailing = 0;
  if (prog_len >= 2 && prog[prog_len - 1] == '\n' && prog[prog_len - 2] == '\\') {
    trailing = 2;
  } else if (prog_len >= 3 && prog[prog_len - 1] == '\n' && prog[prog_len - 2] == '\r' &&
             prog[prog_len - 3] == '\\') {
    trailing = 3;
  }
  if (trailing != 0) {
    size_t line = 1;
    for (size_t i = 0; i < prog_len - trailing; i++) {
      if (prog[i] == '\n') line++;
    }
    preprocessor_error_at(filename, line, "backslash-newline at end of file");
    return false;
  }

  struct SourceCursor cur = {prog, 0, 1, 1, 0};
  cursor_skip_splices(&cur);
  bool in_string = false;
  bool in_char = false;
  bool escape = false;

  // Reserve the maximum output size once so the hot byte-copy loop can write
  // the text and source map directly.
  if (!buffer_reserve(out, prog_len)) goto fail;

  while (prog[cur.index] != 0) {
    char c = prog[cur.index];
    struct SourceMappingEntry loc = {interned, cur.line, cur.column};

    if (!in_string && !in_char) {
      // Remove a // comment up to (not including) its newline; a splice
      // continues the comment onto the next line.
      if (c == '/' && cursor_peek_next(&cur) == '/') {
        if (!buffer_append_char(out, ' ', loc)) goto fail;
        cursor_advance(&cur);
        cursor_advance(&cur);
        while (prog[cur.index] != '\0' && prog[cur.index] != '\n') cursor_advance(&cur);
        continue;
      }

      // Remove a /* */ comment; its newlines become hidden newlines.
      if (c == '/' && cursor_peek_next(&cur) == '*') {
        if (!buffer_append_char(out, ' ', loc)) goto fail;
        cursor_advance(&cur);
        cursor_advance(&cur);
        while (prog[cur.index] != '\0') {
          if (prog[cur.index] == '*' && cursor_peek_next(&cur) == '/') {
            cursor_advance(&cur);
            cursor_advance(&cur);
            break;
          }
          if (prog[cur.index] == '\n') cur.hidden_newlines++;
          cursor_advance(&cur);
        }
        continue;
      }

      if (c == '"') {
        in_string = true;
      } else if (c == '\'') {
        in_char = true;
      }
    } else {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (in_string && c == '"') {
        in_string = false;
      } else if (in_char && c == '\'') {
        in_char = false;
      }
    }

    if (!source_mapping_append(&out->map, loc, 1, true)) goto fail;
    out->data[out->len] = c;
    out->len++;
    if (c == '\n') {
      // C literals cannot span lines; an unterminated one (diagnosed later by
      // the lexer) must not hide comments on following lines.
      in_string = false;
      in_char = false;
      escape = false;
      if (!flush_hidden_newlines(&cur, out, loc)) goto fail;
    }
    cursor_advance(&cur);
  }

  // Keep the source's line count when the last logical line had no newline.
  struct SourceMappingEntry end_loc = {interned, cur.line, cur.column};
  if (!flush_hidden_newlines(&cur, out, end_loc)) goto fail;

  if (!buffer_finish(out)) goto fail;
  return true;

fail:
  fprintf(stderr, "Preprocessor memory error\n");
  return false;
}

// Read an entire file into a newly allocated buffer.
// Returns a NUL-terminated buffer or NULL on failure.
// Caller owns the returned buffer and must free it.
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

// Heap-allocate a copy of the provided string.
// src is a NUL-terminated string.
// Returns a heap-allocated duplicate or NULL on failure.
// Caller owns the returned string.
static char* copy_string(const char* src) {
  size_t len = strlen(src);
  char* out = malloc(len + 1);
  if (out == NULL) return NULL;
  memcpy(out, src, len);
  out[len] = '\0';
  return out;
}

// Initialize ownership for filenames interned during preprocessing.
// Caller releases all stored strings with file_table_destroy.
static void file_table_init(struct FileTable* table) {
  table->names = NULL;
  table->count = 0;
  table->cap = 0;
}

// Intern a filename string and return a stable pointer.
// Returns a pointer to a stored copy or NULL on allocation failure.
// Returned pointer remains valid until file_table_destroy.
static const char* file_table_intern(struct FileTable* table, const char* name) {
  for (size_t i = 0; i < table->count; ++i) {
    if (strcmp(table->names[i], name) == 0) {
      return table->names[i];
    }
  }

  size_t new_cap = table->cap == 0 ? FILE_TABLE_INITIAL_CAP : table->cap;
  if (table->count == table->cap) {
    while (new_cap <= table->count) new_cap *= 2;
    char** next = realloc(table->names, new_cap * sizeof(*table->names));
    if (next == NULL) return NULL;
    table->names = next;
    table->cap = new_cap;
  }

  char* copy = copy_string(name);
  if (copy == NULL) return NULL;
  table->names[table->count++] = copy;
  return copy;
}

// Release all filenames stored in the file table.
// Safe to call on empty tables.
static void file_table_destroy(struct FileTable* table) {
  for (size_t i = 0; i < table->count; ++i) {
    free(table->names[i]);
  }
  free(table->names);
  table->names = NULL;
  table->count = 0;
  table->cap = 0;
}

// Resolve include paths relative to the current file (no <...> support).
// Returns a heap-allocated resolved path.
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
// Returns true when the name is a valid identifier.
static bool is_valid_macro_name(const char* name, size_t len) {
  if (len == 0 || !is_ident_start(name[0])) return false;
  for (size_t i = 1; i < len; ++i) {
    if (!is_ident_char(name[i])) return false;
  }
  return true;
}

// Parse the text after "#define" and store the macro. Defined below.
static bool parse_define_line(const char* line, const char* line_end,
                              const char* filename, size_t line_no,
                              struct Macro** macros);

// Pseudo-filename used in diagnostics for -D definitions.
static const char kCommandLineFilename[] = "<command line>";

// Apply -DNAME, -DNAME=value, and -DNAME(params)=value definitions from the
// command line. A missing value defines the macro as 1, as with cc -D.
// Returns true on success and updates the macro list.
// defines entries are NUL-terminated strings.
static bool apply_cli_defines(struct Macro** macros, int num_defines, const char* const* defines) {
  for (int i = 0; i < num_defines; ++i) {
    const char* def = defines[i];
    if (def == NULL || def[0] == '\0') {
      fprintf(stderr, "Invalid -D definition\n");
      return false;
    }

    const char* eq = strchr(def, '=');
    size_t shown_len = eq == NULL ? strlen(def) : (size_t)(eq - def);
    size_t name_len = 0;
    while (is_ident_char(def[name_len])) name_len++;
    char after_name = def[name_len];
    if (!is_valid_macro_name(def, name_len) ||
        (after_name != '\0' && after_name != '=' && after_name != '(')) {
      fprintf(stderr, "Invalid -D name: %.*s\n", (int)shown_len, def);
      return false;
    }
    if (is_builtin_macro_name(def, name_len)) {
      fprintf(stderr, "Invalid -D name: %.*s (reserved builtin macro)\n", (int)name_len, def);
      return false;
    }

    // Rewrite to #define syntax: "NAME[(params)]=value" -> "NAME[(params)] value".
    // A parameter list cannot contain '=', so the first '=' ends the name part.
    size_t def_len = strlen(def);
    char* text = malloc(def_len + 3);
    if (text == NULL) {
      fprintf(stderr, "Preprocessor memory error while applying -D%s\n", def);
      return false;
    }
    memcpy(text, def, def_len + 1);
    size_t text_len = def_len;
    if (eq != NULL) {
      text[eq - def] = ' ';
    } else {
      memcpy(text + def_len, " 1", 3);
      text_len += 2;
    }
    bool ok = parse_define_line(text, text + text_len, kCommandLineFilename, 1, macros);
    free(text);
    if (!ok) return false;
  }
  return true;
}

// Append a size_t as a decimal string.
// Returns true on success and appends the decimal digits.
static bool buffer_append_line_number(struct Buffer* out, size_t line_no, struct SourceMappingEntry loc) {
  char digits[LINE_NUMBER_BUFFER_CAP];
  size_t len = 0;
  size_t value = line_no;

  do {
    digits[len++] = (char)('0' + (value % 10));
    value /= 10;
  } while (value != 0);

  for (size_t i = 0; i < len; ++i) {
    if (!buffer_append_char(out, digits[len - 1 - i], loc)) return false;
  }
  return true;
}

// Append a C string literal with minimal escaping.
// Returns true on success and appends a quoted string literal.
static bool buffer_append_c_string_literal(struct Buffer* out, const char* value, struct SourceMappingEntry loc) {
  if (!buffer_append_char(out, '"', loc)) return false;
  for (const char* p = value; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (!buffer_append_char(out, '\\', loc)) return false;
    }
    if (!buffer_append_char(out, *p, loc)) return false;
  }
  return buffer_append_char(out, '"', loc);
}

// Expand builtin macros that depend on file/line context.
static bool try_expand_builtin_macro(const char* name, size_t len,
                                     const struct SourceMappingEntry* loc,
                                     struct Buffer* out, bool* matched) {
  *matched = false;
  if (len == kBuiltinFileMacroLen && strncmp(name, kBuiltinFileMacro, kBuiltinFileMacroLen) == 0) {
    *matched = true;
    return buffer_append_c_string_literal(out, loc->filename, *loc);
  }
  if (len == kBuiltinLineMacroLen && strncmp(name, kBuiltinLineMacro, kBuiltinLineMacroLen) == 0) {
    *matched = true;
    return buffer_append_line_number(out, loc->line, *loc);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Macro expansion engine.
//
// Plain source text is copied in bulk by expand_macros_in_span. When it meets
// an invocation it switches to token lists: the macro name becomes a one-token
// list that expand_list rescans until no expandable identifier remains. All
// tokens, hide sets, and synthesized text for one top-level invocation live in
// a scratch arena that is reset once the expansion has been written out.
// ---------------------------------------------------------------------------

// Set of macros whose expansion produced a token (C11 6.10.3.4 "painted"
// names). Lists are immutable, share tails, and live in the scratch arena.
struct HideSet {
  const struct Macro* macro;
  const struct HideSet* next;
};

// One preprocessing token during expansion. Tokens read from the source span
// keep src_offset so they map back to their exact columns; tokens created by
// expansion map every byte to loc, the outermost invocation site.
struct PPToken {
  enum PPTokenKind kind;
  const char* text;
  size_t len;
  bool space_before;
  bool from_source;
  size_t src_offset;
  struct SourceMappingEntry loc;
  const struct HideSet* hide;
  struct PPToken* next;
};

// One argument of a function-like invocation. The macro-expanded form is
// computed on first use because operands of # and ## are never expanded.
struct MacroArg {
  struct PPToken* tokens;
  struct PPToken* expanded;
  bool has_expanded;
};

// Expansion state for one span of active source lines. When an invocation
// needs more tokens than its pending list holds (its argument list continues
// past the expansion that produced its name, or past the end of a line), they
// are lexed from src[pos, end), which never extends past a directive line.
struct Expander {
  const char* src;
  const struct SourceMapping* src_map;
  size_t pos;
  size_t end;
  struct Macro* macros;
};

// Allocate from the scratch arena selected by expand_invocation. Reports the
// failure itself so callers only propagate false.
static void* pp_alloc(size_t size) {
  void* p = arena_alloc(size);
  if (p == NULL) fprintf(stderr, "Preprocessor memory error while expanding macros\n");
  return p;
}

// Original location of a token: its own column for source tokens, otherwise
// the invocation site recorded when it was created.
static struct SourceMappingEntry pp_token_loc(const struct Expander* ex,
                                              const struct PPToken* tok) {
  return tok->from_source ? source_mapping_lookup(ex->src_map, tok->src_offset) : tok->loc;
}

static bool pp_token_is(const struct PPToken* tok, const char* punct) {
  return pp_text_is_punct(tok->kind, tok->text, tok->len, punct);
}

// Create a token that does not come from the source span.
static struct PPToken* pp_new_token(enum PPTokenKind kind, const char* text, size_t len,
                                    struct SourceMappingEntry loc) {
  struct PPToken* tok = pp_alloc(sizeof(*tok));
  if (tok == NULL) return NULL;
  tok->kind = kind;
  tok->text = text;
  tok->len = len;
  tok->space_before = false;
  tok->from_source = false;
  tok->src_offset = 0;
  tok->loc = loc;
  tok->hide = NULL;
  tok->next = NULL;
  return tok;
}

// Copy a token list. Expansion relinks and edits tokens, so every use of a
// macro argument gets its own copy. An empty list copies to NULL.
static bool pp_copy_list(const struct PPToken* list, struct PPToken** out) {
  struct PPToken head;
  head.next = NULL;
  struct PPToken* tail = &head;
  for (const struct PPToken* tok = list; tok != NULL; tok = tok->next) {
    struct PPToken* copy = pp_alloc(sizeof(*copy));
    if (copy == NULL) return false;
    *copy = *tok;
    copy->next = NULL;
    tail->next = copy;
    tail = copy;
  }
  *out = head.next;
  return true;
}

static bool hideset_contains(const struct HideSet* hs, const struct Macro* m) {
  for (; hs != NULL; hs = hs->next) {
    if (hs->macro == m) return true;
  }
  return false;
}

// Return hs plus m, sharing hs's nodes.
static bool hideset_add(const struct HideSet* hs, const struct Macro* m,
                        const struct HideSet** out) {
  if (hideset_contains(hs, m)) {
    *out = hs;
    return true;
  }
  struct HideSet* node = pp_alloc(sizeof(*node));
  if (node == NULL) return false;
  node->macro = m;
  node->next = hs;
  *out = node;
  return true;
}

static bool hideset_union(const struct HideSet* a, const struct HideSet* b,
                          const struct HideSet** out) {
  const struct HideSet* result = b;
  for (; a != NULL; a = a->next) {
    if (!hideset_add(result, a->macro, &result)) return false;
  }
  *out = result;
  return true;
}

static bool hideset_intersect(const struct HideSet* a, const struct HideSet* b,
                              const struct HideSet** out) {
  const struct HideSet* result = NULL;
  for (; a != NULL; a = a->next) {
    if (hideset_contains(b, a->macro) && !hideset_add(result, a->macro, &result)) {
      return false;
    }
  }
  *out = result;
  return true;
}

// Lex the next source token into a new token; *out is NULL at the end of the
// span. Skipped whitespace (including newlines) becomes space_before.
static bool pp_lex_source(struct Expander* ex, struct PPToken** out) {
  bool space = false;
  while (ex->pos < ex->end && is_pp_space(ex->src[ex->pos])) {
    ex->pos++;
    space = true;
  }
  *out = NULL;
  if (ex->pos >= ex->end) return true;

  struct PPToken* tok = pp_alloc(sizeof(*tok));
  if (tok == NULL) return false;
  tok->text = ex->src + ex->pos;
  tok->len = pp_token_length(tok->text, ex->end - ex->pos, &tok->kind);
  tok->space_before = space;
  tok->from_source = true;
  tok->src_offset = ex->pos;
  tok->loc = (struct SourceMappingEntry){NULL, 0, 0};
  tok->hide = NULL;
  tok->next = NULL;
  ex->pos += tok->len;
  *out = tok;
  return true;
}

// Detach the next token of an invocation: from the pending list while it has
// tokens, then (only when use_source) from the unread source. *out is NULL
// when both are exhausted.
static bool pp_take(struct Expander* ex, struct PPToken** rest, bool use_source,
                    struct PPToken** out) {
  if (*rest != NULL) {
    *out = *rest;
    *rest = (*rest)->next;
    (*out)->next = NULL;
    return true;
  }
  *out = NULL;
  if (!use_source) return true;
  return pp_lex_source(ex, out);
}

// Report whether the token after a function-like macro name is '('. The
// pending list is consulted first because it precedes the unread source.
static bool pp_next_is_lparen(const struct Expander* ex, const struct PPToken* rest,
                              bool use_source) {
  if (rest != NULL) return pp_token_is(rest, "(");
  if (!use_source) return false;
  size_t p = ex->pos;
  while (p < ex->end && is_pp_space(ex->src[p])) p++;
  return p < ex->end && ex->src[p] == '(';
}

// Collect the arguments of an invocation of m. On entry the next token
// (pending list, then source) is the '(' that pp_next_is_lparen found. On
// success *rest holds the tokens after the matching ')' and *rparen_out is
// that ')'. Commas nested in parentheses do not separate arguments, nor do
// commas within the variable arguments of a variadic macro.
static bool collect_macro_args(struct Expander* ex, const struct Macro* m,
                               struct SourceMappingEntry origin,
                               struct PPToken** rest, bool use_source,
                               struct MacroArg** args_out, struct PPToken** rparen_out) {
  struct PPToken* tok = NULL;
  if (!pp_take(ex, rest, use_source, &tok)) return false;

  size_t slots = m->param_count > 0 ? m->param_count : 1;
  struct MacroArg* args = pp_alloc(slots * sizeof(*args));
  if (args == NULL) return false;
  for (size_t i = 0; i < slots; ++i) {
    args[i].tokens = NULL;
    args[i].expanded = NULL;
    args[i].has_expanded = false;
  }

  // count is the number of arguments seen so far; "()" is one empty argument.
  // Tokens of arguments beyond the slots are dropped; the count check below
  // reports the invocation.
  size_t count = 1;
  struct PPToken* tail = NULL;
  size_t depth = 0;
  while (true) {
    if (!pp_take(ex, rest, use_source, &tok)) return false;
    if (tok == NULL) {
      preprocessor_error_at(origin.filename, origin.line,
                            "unterminated argument list invoking macro \"%s\": expected ')' %s",
                            m->name,
                            use_source ? "before the next preprocessor directive or end of file"
                                       : "within the enclosing macro argument");
      return false;
    }
    if (pp_token_is(tok, ")")) {
      if (depth == 0) break;
      depth--;
    } else if (pp_token_is(tok, "(")) {
      depth++;
    } else if (pp_token_is(tok, ",") && depth == 0 &&
               !(m->variadic && count == m->param_count)) {
      count++;
      tail = NULL;
      continue;
    }
    if (count <= slots) {
      if (tail == NULL) {
        args[count - 1].tokens = tok;
      } else {
        tail->next = tok;
      }
      tail = tok;
    }
  }
  *rparen_out = tok;

  size_t given = count;
  if (m->param_count == 0 && count == 1 && args[0].tokens == NULL) given = 0;
  // Omitting the variable arguments entirely is accepted (C23 6.10.5p4;
  // C11 required at least one).
  bool ok = given == m->param_count || (m->variadic && given == m->param_count - 1);
  if (!ok) {
    size_t required = m->variadic ? m->param_count - 1 : m->param_count;
    preprocessor_error_at(origin.filename, origin.line,
                          "macro \"%s\" requires %s%zu argument%s, but %zu %s given",
                          m->name, m->variadic ? "at least " : "", required,
                          required == 1 ? "" : "s", given, given == 1 ? "was" : "were");
    return false;
  }
  *args_out = args;
  return true;
}

// Build the string literal for "# param" (C11 6.10.3.2): tokens joined by one
// space wherever whitespace separated them, with '"' and '\' escaped inside
// string literals and character constants.
static struct PPToken* stringize_arg(const struct PPToken* arg, struct SourceMappingEntry loc) {
  size_t len = 2;
  for (const struct PPToken* t = arg; t != NULL; t = t->next) {
    if (t != arg && t->space_before) len++;
    len += t->len;
    if (t->kind == PP_LITERAL) {
      for (size_t i = 0; i < t->len; ++i) {
        if (t->text[i] == '"' || t->text[i] == '\\') len++;
      }
    }
  }

  char* text = pp_alloc(len);
  if (text == NULL) return NULL;
  size_t n = 0;
  text[n++] = '"';
  for (const struct PPToken* t = arg; t != NULL; t = t->next) {
    if (t != arg && t->space_before) text[n++] = ' ';
    for (size_t i = 0; i < t->len; ++i) {
      char c = t->text[i];
      if (t->kind == PP_LITERAL && (c == '"' || c == '\\')) text[n++] = '\\';
      text[n++] = c;
    }
  }
  text[n++] = '"';
  return pp_new_token(PP_LITERAL, text, n, loc);
}

// Replace lhs in place with the concatenation lhs ## rhs (C11 6.10.3.3). The
// result must lex as exactly one preprocessing token.
static bool paste_tokens(struct PPToken* lhs, const struct PPToken* rhs,
                         const struct Macro* m, struct SourceMappingEntry origin) {
  size_t len = lhs->len + rhs->len;
  char* text = pp_alloc(len);
  if (text == NULL) return false;
  memcpy(text, lhs->text, lhs->len);
  memcpy(text + lhs->len, rhs->text, rhs->len);

  enum PPTokenKind kind;
  if (pp_token_length(text, len, &kind) != len) {
    preprocessor_error_at(origin.filename, origin.line,
                          "pasting \"%.*s\" and \"%.*s\" in macro \"%s\" does not give a "
                          "valid preprocessing token",
                          (int)lhs->len, lhs->text, (int)rhs->len, rhs->text, m->name);
    return false;
  }
  lhs->kind = kind;
  lhs->text = text;
  lhs->len = len;
  lhs->from_source = false;
  lhs->loc = origin;
  return true;
}

static bool expand_list(struct Expander* ex, struct PPToken* list, bool use_source,
                        struct PPToken** out);

// Return arg fully macro-expanded on its own, as if it formed the rest of the
// file (C11 6.10.3.1), so an invocation inside it cannot consume tokens after it.
static bool expanded_arg(struct Expander* ex, struct MacroArg* arg, struct PPToken** out) {
  if (!arg->has_expanded) {
    struct PPToken* copy = NULL;
    if (!pp_copy_list(arg->tokens, &copy)) return false;
    if (!expand_list(ex, copy, false, &arg->expanded)) return false;
    arg->has_expanded = true;
  }
  *out = arg->expanded;
  return true;
}

// Return the argument bound to parameter `param` of the invocation of m being
// substituted. The definition checks guarantee that only function-like macros,
// whose invocations always collect arguments, reference parameters; this
// rechecks that contract so a violation is reported instead of reading
// through a NULL or out-of-range argument array.
static struct MacroArg* macro_param_arg(const struct Macro* m, struct MacroArg* args, int param,
                                        struct SourceMappingEntry origin) {
  if (args == NULL || param < 0 || (size_t)param >= m->param_count) {
    preprocessor_error_at(origin.filename, origin.line,
                          "internal error: replacement list of macro \"%s\" references "
                          "parameter %d, which the invocation does not supply (%zu "
                          "parameters, %s)",
                          m->name, param, m->param_count,
                          args == NULL ? "no argument list" : "arguments collected");
    return NULL;
  }
  return &args[param];
}

// Build the replacement tokens for one invocation of m: substitute parameters
// (expanded unless an operand of # or ##), stringize, and paste. args is NULL
// for object-like macros. New tokens take origin as their location.
//
// Operands of ## are processed left to right. An empty operand acts as the
// standard's placemarker: "placemarker ## x" yields x and "x ## placemarker"
// yields x; the placemarker flag records that the left operand was empty.
static bool substitute_body(struct Expander* ex, const struct Macro* m, struct MacroArg* args,
                            struct SourceMappingEntry origin, struct PPToken** out) {
  struct PPToken head;
  head.next = NULL;
  struct PPToken* last = &head;
  bool placemarker = false;
  // Body index of a __VA_ARGS__ that follows a GNU ", ##" whose comma was
  // kept; it is substituted as a normal (expanded) parameter, not pasted.
  size_t gnu_comma_operand = SIZE_MAX;

  size_t i = 0;
  while (i < m->body_len) {
    if (macro_body_is(m, i, "##")) {
      i++;
      continue;
    }

    const struct MacroBodyToken* bt = &m->body[i];
    if (m->variadic && macro_body_is(m, i, ",") && i + 2 < m->body_len &&
        macro_body_is(m, i + 1, "##") &&
        m->body[i + 2].param == (int)(m->param_count - 1)) {
      struct MacroArg* va_args = macro_param_arg(m, args, m->body[i + 2].param, origin);
      if (va_args == NULL) return false;
      if (va_args->tokens == NULL) {
        // Drop ", ## __VA_ARGS__" entirely; like an empty operand, it is a
        // placemarker if another ## follows.
        i += 3;
        placemarker = true;
        continue;
      }
      gnu_comma_operand = i + 2;
    }

    bool pasted_rhs = i > 0 && macro_body_is(m, i - 1, "##") && i != gnu_comma_operand;
    bool stringize = m->function_like && macro_body_is(m, i, "#");
    size_t op_end = stringize ? i + 2 : i + 1;
    bool pasted_lhs = op_end < m->body_len && macro_body_is(m, op_end, "##") &&
                      op_end + 1 != gnu_comma_operand;

    struct PPToken* op = NULL;
    if (stringize) {
      struct MacroArg* arg = macro_param_arg(m, args, m->body[i + 1].param, origin);
      if (arg == NULL) return false;
      op = stringize_arg(arg->tokens, origin);
      if (op == NULL) return false;
    } else if (bt->param >= 0) {
      struct MacroArg* arg = macro_param_arg(m, args, bt->param, origin);
      if (arg == NULL) return false;
      if (pasted_lhs || pasted_rhs) {
        if (!pp_copy_list(arg->tokens, &op)) return false;
      } else {
        struct PPToken* expanded = NULL;
        if (!expanded_arg(ex, arg, &expanded) || !pp_copy_list(expanded, &op)) return false;
      }
    } else {
      op = pp_new_token(bt->kind, m->value + bt->offset, bt->len, origin);
      if (op == NULL) return false;
    }
    if (op != NULL) op->space_before = bt->space_before;

    if (pasted_rhs && !placemarker) {
      // last is the final token of the non-empty left operand. The definition
      // check rejects a leading "##", so some operand precedes this one.
      if (last == &head) {
        preprocessor_error_at(origin.filename, origin.line,
                              "internal error: '##' in macro \"%s\" has no left operand",
                              m->name);
        return false;
      }
      if (op != NULL) {
        if (!paste_tokens(last, op, m, origin)) return false;
        last->next = op->next;
      }
      placemarker = false;
    } else {
      last->next = op;
      placemarker = op == NULL;
    }
    while (last->next != NULL) last = last->next;
    i = op_end;
  }
  *out = head.next;
  return true;
}

// Create the replacement for __FILE__ or __LINE__ at loc. *out stays NULL
// when tok is not a builtin name.
static bool make_builtin_token(const struct PPToken* tok, struct SourceMappingEntry loc,
                               struct PPToken** out) {
  *out = NULL;
  if (tok->len == kBuiltinLineMacroLen &&
      memcmp(tok->text, kBuiltinLineMacro, kBuiltinLineMacroLen) == 0) {
    char* text = pp_alloc(LINE_NUMBER_BUFFER_CAP);
    if (text == NULL) return false;
    int len = snprintf(text, LINE_NUMBER_BUFFER_CAP, "%zu", loc.line);
    *out = pp_new_token(PP_NUMBER, text, (size_t)len, loc);
    return *out != NULL;
  }
  if (tok->len == kBuiltinFileMacroLen &&
      memcmp(tok->text, kBuiltinFileMacro, kBuiltinFileMacroLen) == 0) {
    size_t name_len = strlen(loc.filename);
    char* text = pp_alloc(name_len * 2 + 2);
    if (text == NULL) return false;
    size_t n = 0;
    text[n++] = '"';
    for (size_t i = 0; i < name_len; ++i) {
      if (loc.filename[i] == '\\' || loc.filename[i] == '"') text[n++] = '\\';
      text[n++] = loc.filename[i];
    }
    text[n++] = '"';
    *out = pp_new_token(PP_LITERAL, text, n, loc);
    return *out != NULL;
  }
  return true;
}

// If the head of *list is an identifier naming a macro that is not in its
// hide set (and, for a function-like macro, is followed by '('), replace it
// and its argument list with the replacement tokens followed by the rest of
// the list, ready to be rescanned, and set *expanded.
static bool expand_head(struct Expander* ex, struct PPToken** list, bool use_source,
                        bool* expanded) {
  struct PPToken* tok = *list;
  *expanded = false;
  if (tok->kind != PP_IDENT) return true;

  struct SourceMappingEntry origin = pp_token_loc(ex, tok);
  struct PPToken* builtin = NULL;
  if (!make_builtin_token(tok, origin, &builtin)) return false;
  if (builtin != NULL) {
    builtin->space_before = tok->space_before;
    builtin->next = tok->next;
    *list = builtin;
    *expanded = true;
    return true;
  }

  const struct Macro* m = macro_find(ex->macros, tok->text, tok->len);
  if (m == NULL || hideset_contains(tok->hide, m)) return true;

  struct PPToken* rest = tok->next;
  const struct HideSet* hide = NULL;
  struct PPToken* body = NULL;
  if (!m->function_like) {
    if (!hideset_add(tok->hide, m, &hide)) return false;
    if (!substitute_body(ex, m, NULL, origin, &body)) return false;
  } else {
    if (!pp_next_is_lparen(ex, rest, use_source)) return true;
    struct MacroArg* args = NULL;
    struct PPToken* rparen = NULL;
    if (!collect_macro_args(ex, m, origin, &rest, use_source, &args, &rparen)) return false;
    // The invocation's hide set is HS(name) & HS(')') plus m (Prosser).
    const struct HideSet* common = NULL;
    if (!hideset_intersect(tok->hide, rparen->hide, &common) ||
        !hideset_add(common, m, &hide)) {
      return false;
    }
    if (!substitute_body(ex, m, args, origin, &body)) return false;
  }

  struct PPToken* tail = NULL;
  for (struct PPToken* t = body; t != NULL; t = t->next) {
    if (!hideset_union(t->hide, hide, &t->hide)) return false;
    tail = t;
  }
  if (body != NULL) {
    body->space_before = tok->space_before;
    tail->next = rest;
    *list = body;
  } else {
    if (rest != NULL && tok->space_before) rest->space_before = true;
    *list = rest;
  }
  *expanded = true;
  return true;
}

// Rescan list until no expandable identifier remains and return the result.
// With use_source, an invocation at the end of the list may read its argument
// list from the unread source span.
static bool expand_list(struct Expander* ex, struct PPToken* list, bool use_source,
                        struct PPToken** out) {
  struct PPToken head;
  head.next = NULL;
  struct PPToken* tail = &head;
  while (list != NULL) {
    bool expanded = false;
    if (!expand_head(ex, &list, use_source, &expanded)) return false;
    if (expanded) continue;
    struct PPToken* tok = list;
    list = tok->next;
    tok->next = NULL;
    tail->next = tok;
    tail = tok;
  }
  *out = head.next;
  return true;
}

// Report whether printing character b directly after character a could make
// the C lexer read different tokens than the expansion produced (for example
// "-" followed by "-1" becoming "--1", or "/" "/" starting a comment), so a
// separating space is required. Conservative: an extra space is harmless.
static bool chars_would_merge(char a, char b) {
  if (is_ident_char(a) && is_ident_char(b)) return true;
  switch (a) {
    case '+': return b == '+' || b == '=';
    case '-': return b == '-' || b == '=' || b == '>';
    case '<': return b == '<' || b == '=';
    case '>': return b == '>' || b == '=';
    case '&': return b == '&' || b == '=';
    case '|': return b == '|' || b == '=';
    case '/': return b == '/' || b == '*' || b == '=';
    case '=': case '!': case '*': case '%': case '^': return b == '=';
    case '#': return b == '#';
    case '.': return b == '.' || isdigit((unsigned char)b);
    default: return false;
  }
}

// Append expanded tokens to out, one space where the expansion had
// whitespace or where adjacent tokens would otherwise merge. The first token
// gets no leading space: the whitespace before the macro name was already
// copied from the source.
static bool emit_expansion(const struct Expander* ex, const struct PPToken* list,
                           struct Buffer* out) {
  for (const struct PPToken* t = list; t != NULL; t = t->next) {
    struct SourceMappingEntry loc = pp_token_loc(ex, t);
    bool space = t != list && t->space_before;
    if (!space && out->len > 0) space = chars_would_merge(out->data[out->len - 1], t->text[0]);
    if (space && !buffer_append_char(out, ' ', loc)) return false;
    if (t->from_source) {
      if (!buffer_append_range(out, ex->src, ex->src_map, t->src_offset, t->len)) return false;
    } else if (!buffer_append_str_with_loc(out, t->text, t->len, t->loc)) {
      return false;
    }
  }
  return true;
}

// Expand the invocation whose macro name is src[name_start, name_end) and
// write the result to out, leaving ex->pos after the last source byte the
// invocation consumed. All scratch storage is released before returning.
static bool expand_invocation(struct Expander* ex, size_t name_start, size_t name_end,
                              struct Arena* scratch, struct Buffer* out) {
  struct Arena* previous = arena_set_current(scratch);
  bool ok = false;
  ex->pos = name_end;
  struct PPToken* name = pp_alloc(sizeof(*name));
  if (name != NULL) {
    name->kind = PP_IDENT;
    name->text = ex->src + name_start;
    name->len = name_end - name_start;
    name->space_before = false;
    name->from_source = true;
    name->src_offset = name_start;
    name->loc = (struct SourceMappingEntry){NULL, 0, 0};
    name->hide = NULL;
    name->next = NULL;

    struct PPToken* result = NULL;
    if (expand_list(ex, name, true, &result)) {
      ok = emit_expansion(ex, result, out);
      // Keep the expansion's last token from merging with the source after it.
      if (ok && ex->pos < ex->end && out->len > 0 &&
          chars_would_merge(out->data[out->len - 1], ex->src[ex->pos])) {
        ok = buffer_append_char(out, ' ', source_mapping_lookup(ex->src_map, ex->pos));
      }
      if (!ok) fprintf(stderr, "Preprocessor memory error while writing macro expansion\n");
    }
  }
  arena_set_current(previous);
  arena_reset(scratch);
  return ok;
}

// Expand macros in src_text[span_start, span_end), a run of consecutive active
// non-directive lines, appending the result to out. Text other than macro
// invocations is copied in mapped runs; literals and pp-numbers are skipped
// whole so identifiers inside them are never expanded. Errors are reported
// before returning false.
static bool expand_macros_in_span(const char* src_text, const struct SourceMapping* src_map,
                                  size_t span_start, size_t span_end,
                                  struct Macro* macros, struct Arena* scratch,
                                  struct Buffer* out) {
  struct Expander ex = {src_text, src_map, span_start, span_end, macros};
  size_t i = span_start;
  while (i < span_end) {
    if (is_ident_start(src_text[i])) {
      size_t start = i++;
      while (i < span_end && is_ident_char(src_text[i])) i++;

      // Look up the location only for builtins; a lookup per identifier is a
      // measurable share of preprocessing time.
      if (is_builtin_macro_name(src_text + start, i - start)) {
        bool matched = false;
        struct SourceMappingEntry name_loc = source_mapping_lookup(src_map, start);
        if (!try_expand_builtin_macro(src_text + start, i - start, &name_loc, out, &matched)) {
          fprintf(stderr, "Preprocessor memory error\n");
          return false;
        }
        continue;
      }

      struct Macro* macro = macro_find(macros, src_text + start, i - start);
      if (macro != NULL) {
        ex.pos = i;
        // A function-like macro name not followed by '(' is an ordinary identifier.
        if (!macro->function_like || pp_next_is_lparen(&ex, NULL, true)) {
          if (!expand_invocation(&ex, start, i, scratch, out)) return false;
          i = ex.pos;
          continue;
        }
      }
      if (!buffer_append_range(out, src_text, src_map, start, i - start)) {
        fprintf(stderr, "Preprocessor memory error\n");
        return false;
      }
      continue;
    }

    size_t start = i;
    while (i < span_end && !is_ident_start(src_text[i])) {
      char c = src_text[i];
      if (c == '"' || c == '\'' || isdigit((unsigned char)c) ||
          (c == '.' && i + 1 < span_end && isdigit((unsigned char)src_text[i + 1]))) {
        enum PPTokenKind kind;
        i += pp_token_length(src_text + i, span_end - i, &kind);
      } else {
        i++;
      }
    }
    if (!buffer_append_range(out, src_text, src_map, start, i - start)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
  }
  return true;
}

// Parse a function-like macro's parameter list; *cursor points just past its
// '(' and is advanced past the matching ')'. "..." may only be last. On
// success the caller frees *params_out.
static bool parse_macro_params(const char** cursor, const char* line_end,
                               const char* filename, size_t line_no,
                               const char* name, size_t name_len,
                               struct MacroParam** params_out, size_t* count_out,
                               bool* variadic_out) {
  static const char kVaArgs[] = "__VA_ARGS__";
  const char* p = *cursor;
  struct MacroParam* params = NULL;
  size_t count = 0;
  size_t cap = 0;
  *variadic_out = false;

  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (p < line_end && *p == ')') {
    p++;
    goto done;
  }

  while (true) {
    while (p < line_end && isspace((unsigned char)*p)) p++;
    if (line_end - p >= 3 && memcmp(p, "...", 3) == 0) {
      p += 3;
      *variadic_out = true;
      while (p < line_end && isspace((unsigned char)*p)) p++;
      if (p >= line_end || *p != ')') {
        preprocessor_error_at(filename, line_no,
                              "expected ')' after \"...\" in parameter list of macro \"%.*s\"",
                              (int)name_len, name);
        goto fail;
      }
      p++;
      break;
    }

    if (p >= line_end || !is_ident_start(*p)) {
      preprocessor_error_at(filename, line_no,
                            "expected parameter name or \"...\" in parameter list of "
                            "macro \"%.*s\"", (int)name_len, name);
      goto fail;
    }
    const char* param = p;
    while (p < line_end && is_ident_char(*p)) p++;
    size_t param_len = (size_t)(p - param);
    if (param_len == sizeof(kVaArgs) - 1 && memcmp(param, kVaArgs, param_len) == 0) {
      preprocessor_error_at(filename, line_no,
                            "__VA_ARGS__ cannot be used as a parameter name of macro \"%.*s\" "
                            "(declare variable arguments with \"...\")",
                            (int)name_len, name);
      goto fail;
    }
    for (size_t i = 0; i < count; ++i) {
      if (params[i].len == param_len && memcmp(params[i].name, param, param_len) == 0) {
        preprocessor_error_at(filename, line_no,
                              "duplicate parameter \"%.*s\" in macro \"%.*s\"",
                              (int)param_len, param, (int)name_len, name);
        goto fail;
      }
    }
    if (count == cap) {
      size_t new_cap = cap == 0 ? 4 : cap * 2;
      struct MacroParam* next = realloc(params, new_cap * sizeof(*next));
      if (next == NULL) {
        preprocessor_error_at(filename, line_no,
                              "memory error while parsing parameters of macro \"%.*s\"",
                              (int)name_len, name);
        goto fail;
      }
      params = next;
      cap = new_cap;
    }
    params[count].name = param;
    params[count].len = param_len;
    count++;

    while (p < line_end && isspace((unsigned char)*p)) p++;
    if (p < line_end && *p == ',') {
      p++;
      continue;
    }
    if (p < line_end && *p == ')') {
      p++;
      break;
    }
    if (p >= line_end) {
      preprocessor_error_at(filename, line_no,
                            "missing ')' in parameter list of macro \"%.*s\"",
                            (int)name_len, name);
    } else {
      preprocessor_error_at(filename, line_no,
                            "expected ',' or ')' after parameter \"%.*s\" of macro \"%.*s\", "
                            "found '%c'", (int)param_len, param, (int)name_len, name, *p);
    }
    goto fail;
  }

done:
  *cursor = p;
  *params_out = params;
  *count_out = count;
  return true;

fail:
  free(params);
  return false;
}

// Parse the text after "#define" and store the macro in the macro list. A
// '(' immediately after the name (no whitespace) starts a function-like
// macro's parameter list (C11 6.10.3p3); otherwise the rest of the line is
// an object-like replacement list. Returns false after reporting an error.
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
  if (is_builtin_macro_name(name_start, name_len)) {
    preprocessor_error_at(filename, line_no,
                          "cannot redefine builtin macro: %.*s",
                          (int)name_len, name_start);
    return false;
  }

  bool function_like = p < line_end && *p == '(';
  struct MacroParam* params = NULL;
  size_t param_count = 0;
  bool variadic = false;
  if (function_like) {
    p++;
    if (!parse_macro_params(&p, line_end, filename, line_no, name_start, name_len,
                            &params, &param_count, &variadic)) {
      return false;
    }
  }

  while (p < line_end && isspace((unsigned char)*p)) p++;
  const char* value_start = p;
  const char* value_end = line_end;
  while (value_end > value_start && isspace((unsigned char)*(value_end - 1))) value_end--;

  bool ok = macro_define(macros, name_start, name_len, value_start,
                         (size_t)(value_end - value_start), function_like, variadic,
                         params, param_count, filename, line_no);
  free(params);
  return ok;
}

// Preprocess a single source buffer (shared macro state).
// Returns a newly allocated preprocessed buffer or NULL on failure.
// The macros list is owned by the caller.
static bool preprocess_buffer(const char* prog, const char* filename,
                              struct Macro** macros, struct FileTable* files,
                              struct PreprocessOutput* out);

// Preprocess an included source file and splice its tokens into the current input.
// Returns true on success and appends included text to out.
static bool handle_include_line(const char* line, const char* line_end, const char* filename,
                                size_t line_no, struct Macro** macros, struct FileTable* files,
                                struct Buffer* out, struct SourceMappingEntry newline_loc,
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

  struct PreprocessOutput include_output = {0};
  if (!preprocess_buffer(include_source, include_path, macros, files, &include_output)) {
    free(include_source);
    free(include_path);
    return false;
  }
  free(include_source);
  free(include_path);
  size_t include_len = include_output.map.length;
  bool ok = buffer_append_range(out, include_output.text, &include_output.map, 0, include_len);
  if (ok && add_newline && (include_len == 0 || include_output.text[include_len - 1] != '\n')) {
    ok = buffer_append_char(out, '\n', newline_loc);
  }
  free(include_output.text);
  source_mapping_free(&include_output.map);
  return ok;
}

// Parse and validate the identifier in #ifdef/#ifndef/#undef directives.
// Returns true on success and fills name_start/name_len.
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

// Parse a "defined" term used in #if expressions.
// Returns true on success and fills out_value/next.
static bool parse_if_defined_term(const char* line, const char* line_end,
                                  const char* filename, size_t line_no,
                                  struct Macro* macros,
                                  bool* out_value, const char** next) {
  const char* p = line;
  while (p < line_end && isspace((unsigned char)*p)) p++;
  bool negate = false;
  if (p < line_end && *p == '!') {
    negate = true;
    p++;
    while (p < line_end && isspace((unsigned char)*p)) p++;
  }

  if (line_end - p < 7 || strncmp(p, "defined", 7) != 0) {
    preprocessor_error_at(filename, line_no, "invalid #if directive (expected defined)");
    return false;
  }
  p += 7;
  while (p < line_end && isspace((unsigned char)*p)) p++;

  bool has_paren = false;
  if (p < line_end && *p == '(') {
    has_paren = true;
    p++;
    while (p < line_end && isspace((unsigned char)*p)) p++;
  }

  if (p >= line_end || !is_ident_start(*p)) {
    preprocessor_error_at(filename, line_no, "invalid #if directive (expected identifier)");
    return false;
  }
  const char* name_start = p;
  p++;
  while (p < line_end && is_ident_char(*p)) p++;
  size_t name_len = (size_t)(p - name_start);

  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (has_paren) {
    if (p >= line_end || *p != ')') {
      preprocessor_error_at(filename, line_no, "invalid #if directive (missing ')')");
      return false;
    }
    p++;
  }

  bool is_defined = is_macro_defined(macros, name_start, name_len);
  *out_value = negate ? !is_defined : is_defined;
  *next = p;
  return true;
}

// Parse a limited #if condition with defined/!defined and &&.
// Returns true on success and fills out_value.
static bool parse_if_condition(const char* line, const char* line_end,
                               const char* filename, size_t line_no,
                               struct Macro* macros, bool* out_value) {
  const char* p = line;
  bool value = false;
  if (!parse_if_defined_term(p, line_end, filename, line_no, macros, &value, &p)) {
    return false;
  }

  while (true) {
    while (p < line_end && isspace((unsigned char)*p)) p++;
    if (p + 1 < line_end && p[0] == '&' && p[1] == '&') {
      p += 2;
      bool rhs = false;
      if (!parse_if_defined_term(p, line_end, filename, line_no, macros, &rhs, &p)) {
        return false;
      }
      value = value && rhs;
      continue;
    }
    break;
  }

  while (p < line_end && isspace((unsigned char)*p)) p++;
  if (p != line_end) {
    preprocessor_error_at(filename, line_no, "invalid #if directive (unexpected tokens)");
    return false;
  }

  *out_value = value;
  return true;
}

// Parse and apply one preprocessor directive line.
// Returns true on success and updates macros/output buffers as needed.
static bool preprocess_directive(
    const char* line_start,
    const char* line_end,
    struct SourceMappingEntry newline_loc,
    bool has_newline,
    size_t line_no,
    const char* filename,
    struct Macro** macros,
    struct FileTable* files,
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
    return handle_include_line(p, line_end, filename, line_no, macros, files,
                               out, newline_loc, has_newline);
  }

  if (word_len == 6 && strncmp(word_start, "define", 6) == 0) {
    if (!is_active) return true;
    return parse_define_line(p, line_end, filename, line_no, macros);
  }

  if (word_len == 5 && strncmp(word_start, "undef", 5) == 0) {
    if (!is_active) return true;
    const char* name_start = NULL;
    size_t name_len = 0;
    if (!parse_ifdef_name(p, line_end, filename, line_no, "undef", &name_start, &name_len)) {
      return false;
    }
    const char* rest = name_start + name_len;
    while (rest < line_end && isspace((unsigned char)*rest)) rest++;
    if (rest != line_end) {
      preprocessor_error_at(filename, line_no,
                            "unexpected tokens after macro name \"%.*s\" in #undef directive",
                            (int)name_len, name_start);
      return false;
    }
    if (is_builtin_macro_name(name_start, name_len)) {
      preprocessor_error_at(filename, line_no, "cannot undefine builtin macro: %.*s",
                            (int)name_len, name_start);
      return false;
    }
    macro_undefine(macros, name_start, name_len);
    return true;
  }

  if (word_len == 2 && strncmp(word_start, "if", 2) == 0) {
    bool condition_true = false;
    if (if_stack->current_active) {
      if (!parse_if_condition(p, line_end, filename, line_no, *macros, &condition_true)) {
        return false;
      }
    } else {
      bool ignored = false;
      if (!parse_if_condition(p, line_end, filename, line_no, *macros, &ignored)) {
        return false;
      }
      condition_true = false;
    }
    if (!ifstack_push(if_stack, condition_true)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
    return true;
  }

  if (word_len == 5 && strncmp(word_start, "ifdef", 5) == 0) {
    const char* name_start = NULL;
    size_t name_len = 0;
    if (!parse_ifdef_name(p, line_end, filename, line_no, "ifdef", &name_start, &name_len)) {
      return false;
    }
    bool condition_true = false;
    if (if_stack->current_active) {
      condition_true = is_macro_defined(*macros, name_start, name_len);
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
      condition_true = !is_macro_defined(*macros, name_start, name_len);
    }
    if (!ifstack_push(if_stack, condition_true)) {
      fprintf(stderr, "Preprocessor memory error\n");
      return false;
    }
    return true;
  }

  if (word_len == 6 && strncmp(word_start, "pragma", 6) == 0) {
    // Ignore pragmas in this minimal preprocessor.
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

// Strip comments, apply directives, and expand macros.
// Consecutive active non-directive lines form one span that is expanded as a
// unit, so a macro invocation's argument list may continue onto later lines;
// a span ends at the next directive line, which an argument list cannot cross.
// Returns true on success and fills out with preprocessed data.
static bool preprocess_buffer(const char* prog, const char* filename,
                              struct Macro** macros, struct FileTable* files,
                              struct PreprocessOutput* out) {
  struct Buffer no_comments;
  size_t prog_len = strlen(prog);
  size_t initial_cap = prog_len + 1;
  if (initial_cap < 64) initial_cap = 64;
  if (!buffer_init(&no_comments, initial_cap)) {
    fprintf(stderr, "Preprocessor memory error\n");
    return false;
  }
  if (!strip_comments(prog, prog_len, filename, files, &no_comments)) {
    buffer_free(&no_comments);
    return false;
  }

  struct Buffer output;
  size_t output_cap = no_comments.len + 1;
  if (output_cap < 64) output_cap = 64;
  if (!buffer_init(&output, output_cap)) {
    fprintf(stderr, "Preprocessor memory error\n");
    buffer_free(&no_comments);
    return false;
  }

  // Scratch storage for macro expansion, reset after every invocation.
  // arena_create exits the compiler if it cannot allocate the header.
  struct Arena* scratch = arena_create(PREPROCESS_SCRATCH_BLOCK_SIZE);

  // Track nesting and whether we're currently emitting active regions.
  struct IfStack if_stack;
  if_stack.items = NULL;
  if_stack.count = 0;
  if_stack.cap = 0;
  if_stack.current_active = true;

  // Pending span of active text lines: no_comments.data[span_start, line start).
  bool span_open = false;
  size_t span_start = 0;

  const char* cursor = no_comments.data;
  size_t line_no = 1;
  while (*cursor != '\0') {
    const char* line_start = cursor;
    while (*cursor != '\0' && *cursor != '\n') cursor++;
    const char* line_end = cursor;
    bool has_newline = (*cursor == '\n');
    if (has_newline) cursor++;
    size_t line_len = (size_t)(line_end - line_start);
    size_t line_offset = (size_t)(line_start - no_comments.data);

    const char* p = line_start;
    while (p < line_end && isspace((unsigned char)*p)) p++;
    // Directive lines are recognized even when indented.
    if (p < line_end && *p == '#') {
      // Flush text before the directive so its output (e.g. an #include)
      // lands in order and macro definitions apply only to later lines.
      if (span_open) {
        span_open = false;
        if (!expand_macros_in_span(no_comments.data, &no_comments.map, span_start,
                                   line_offset, *macros, scratch, &output)) {
          goto fail;
        }
      }
      struct SourceMappingEntry newline_loc = {NULL, 0, 0};
      if (has_newline) {
        newline_loc = source_mapping_lookup(&no_comments.map, line_offset + line_len);
      }
      if (!preprocess_directive(p + 1, line_end, newline_loc, has_newline, line_no,
                                filename, macros, files, &output, &if_stack)) {
        goto fail;
      }
      if (has_newline) line_no++;
      continue;
    }

    // Only emit non-directive lines from active regions. Activity changes
    // only at directives, so an open span never contains inactive lines.
    if (if_stack.current_active && !span_open) {
      span_open = true;
      span_start = line_offset;
    }
    if (has_newline) line_no++;
  }

  if (span_open &&
      !expand_macros_in_span(no_comments.data, &no_comments.map, span_start,
                             no_comments.len, *macros, scratch, &output)) {
    goto fail;
  }

  // Unterminated #ifdef/#ifndef should be reported as an error.
  if (if_stack.count != 0) {
    preprocessor_error_at(filename, line_no,
                          "unterminated #ifdef/#ifndef block (reached end of file)");
    goto fail;
  }

  arena_free(scratch);
  buffer_free(&no_comments);
  free(if_stack.items);
  if (!buffer_finish(&output)) {
    fprintf(stderr, "Preprocessor memory error\n");
    buffer_free(&output);
    return false;
  }
  // Trim growth slack: the text and map live for the rest of compilation.
  char* trimmed = realloc(output.data, output.len + 1);
  if (trimmed != NULL) output.data = trimmed;
  source_mapping_shrink(&output.map);
  out->text = output.data;
  out->map = output.map;
  return true;

fail:
  arena_free(scratch);
  buffer_free(&no_comments);
  buffer_free(&output);
  free(if_stack.items);
  return false;
}

// Public entrypoint: apply CLI defines, then preprocess the buffer.
// Returns true on success and fills result; false on failure.
// Caller must free result via destroy_preprocess_result.
bool preprocess(char const* prog, const char* filename, int num_defines,
                const char* const* defines, struct PreprocessResult* result) {
  if (result == NULL) return false;
  result->text = NULL;
  result->map.runs = NULL;
  result->map.run_count = 0;
  result->map.run_cap = 0;
  result->map.length = 0;
  file_table_init(&result->file_table);

  struct Macro* macros = NULL;
  if (!apply_cli_defines(&macros, num_defines, defines)) {
    destroy_macros(macros);
    file_table_destroy(&result->file_table);
    return false;
  }

  struct PreprocessOutput output = {0};
  if (!preprocess_buffer(prog, filename, &macros, &result->file_table, &output)) {
    destroy_macros(macros);
    file_table_destroy(&result->file_table);
    return false;
  }

  destroy_macros(macros);
  result->text = output.text;
  result->map = output.map;
  return true;
}

// Free all storage owned by a PreprocessResult.
// Safe to call with partially initialized results.
void destroy_preprocess_result(struct PreprocessResult* result) {
  if (result == NULL) return;
  free(result->text);
  result->text = NULL;
  source_mapping_free(&result->map);
  file_table_destroy(&result->file_table);
}
