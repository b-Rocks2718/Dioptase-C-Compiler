#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "slice.h"
#include "token.h"
#include "token_array.h"
#include "lexer.h"
#include "source_location.h"

// Tokenize preprocessed source into a stream of tokens.
// Uses the global cursor to walk the NUL-terminated source buffer.
// Whitespace is skipped; no comments remain (preprocessed).

static char const * program;
static char const * current;
// Token array being filled by lex(); owns identifier and string payload slices.
static struct TokenArray* lex_tokens;
// Define numeric literal bounds for target integer sizes.
static const uint64_t kIntBits = 32;
static const uint64_t kLongBits = 64;
static const uint64_t kIntMax = (UINT64_C(1) << (kIntBits - 1)) - 1;
static const uint64_t kUIntMax = (UINT64_C(1) << kIntBits) - 1;
static const uint64_t kLongMax = (UINT64_C(1) << (kLongBits - 1)) - 1;
// Distinguish a diagnosed malformed literal from an ordinary token miss.
// Reset at the start of every lex() invocation.
static bool lexer_failed;

// Convert one hexadecimal digit into its numeric value.
// Returns 0-15 for hexadecimal digits, or -1 for any other character.
static int hex_digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Decode a byte-valued \x hexadecimal escape.
// On success, writes the byte value to out_value, the number of consumed
// hex digits to consumed_digits, and returns true.
// The Dioptase C compiler models \x escapes as single-byte
// values, so anything above 0xff is rejected instead of silently truncating.
static bool decode_hex_escape(const char* digits, size_t* consumed_digits,
                              unsigned char* out_value) {
  unsigned int value = 0;
  size_t len = 0;
  while (true) {
    int digit = hex_digit_value(digits[len]);
    if (digit < 0) break;

    unsigned int next_value = (value << 4) | (unsigned int)digit;
    if (next_value > 0xff) {
      *consumed_digits = len;
      return false;
    }

    value = next_value;
    len += 1;
  }

  *consumed_digits = len;
  if (len == 0) return false;

  *out_value = (unsigned char)value;
  return true;
}

// Emit a lexer error diagnostic at an explicit source pointer.
// Writes an error message and the remaining source line to stdout.
static void print_error_at(const char* ptr, const char* fmt, ...) {
  struct SourceLocation loc = source_location_from_ptr(ptr);
  const char* filename = source_filename_for_ptr(ptr);
  printf("Lexer error at %s:%zu:%zu: ", filename, loc.line, loc.column);
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
  printf("%s\n", ptr);
}

// Emit a lexer error diagnostic for the current cursor.
// Writes an error message to stdout.
static void print_error() {
  struct SourceLocation loc = source_location_from_ptr(current);
  const char* filename = source_filename_for_ptr(current);
  if (*current == '\0') {
    printf("Lexer error at %s:%zu:%zu: unexpected end of input\n",
           filename, loc.line, loc.column);
    return;
  }
  printf("Lexer error at %s:%zu:%zu: unexpected character '%c'\n",
         filename, loc.line, loc.column, *current);
  printf("%s\n", current);
}

// Determine whether the lexer has reached the end of input.
// Returns true when no more non-space characters remain.
static bool is_at_end() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
  if (*current != 0) return false;
  else return true;
}

// Skip over ASCII whitespace characters.
// Advances current past whitespace.
static void skip() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
}

// Consume a fixed string at the current cursor.
// Returns true on match and advances current.
static bool consume(const char* str) {
  skip();
  size_t i = 0;
  while (true) {
    char const expected = str[i];
    char const found = current[i];
    if (expected == 0) {
      /* survived to the end of the expected string */
      current += i;
      return true;
    }
    if (expected != found) {
      return false;
    }
    // assertion: found != 0
    i += 1;
  } 
}

// Compare a scanned identifier span with a keyword spelling.
static bool span_equals(const char* start, size_t len, const char* keyword) {
  size_t keyword_len = strlen(keyword);
  return len == keyword_len && memcmp(start, keyword, len) == 0;
}

// Classify an identifier-shaped span. Dispatching by its first byte avoids
// rescanning the source once for every keyword candidate.
static enum TokenType classify_identifier(const char* start, size_t len) {
  switch (start[0]) {
    case '_':
      if (span_equals(start, len, "__attribute__")) return ATTRIBUTE_TOK;
      break;
    case 'b':
      if (span_equals(start, len, "break")) return BREAK_TOK;
      break;
    case 'c':
      if (span_equals(start, len, "case")) return CASE_TOK;
      if (span_equals(start, len, "char")) return CHAR_TOK;
      if (span_equals(start, len, "const")) return CONST_TOK;
      if (span_equals(start, len, "continue")) return CONTINUE_TOK;
      break;
    case 'd':
      if (span_equals(start, len, "default")) return DEFAULT_TOK;
      if (span_equals(start, len, "do")) return DO_TOK;
      break;
    case 'e':
      if (span_equals(start, len, "else")) return ELSE_TOK;
      if (span_equals(start, len, "enum")) return ENUM_TOK;
      if (span_equals(start, len, "extern")) return EXTERN_TOK;
      break;
    case 'f':
      if (span_equals(start, len, "for")) return FOR_TOK;
      break;
    case 'g':
      if (span_equals(start, len, "goto")) return GOTO_TOK;
      break;
    case 'i':
      if (span_equals(start, len, "if")) return IF_TOK;
      if (span_equals(start, len, "int")) return INT_TOK;
      break;
    case 'l':
      if (span_equals(start, len, "long")) return LONG_TOK;
      break;
    case 'r':
      if (span_equals(start, len, "return")) return RETURN_TOK;
      break;
    case 's':
      if (span_equals(start, len, "short")) return SHORT_TOK;
      if (span_equals(start, len, "signed")) return SIGNED_TOK;
      if (span_equals(start, len, "sizeof")) return SIZEOF_TOK;
      if (span_equals(start, len, "static")) return STATIC_TOK;
      if (span_equals(start, len, "struct")) return STRUCT_TOK;
      if (span_equals(start, len, "switch")) return SWITCH_TOK;
      break;
    case 'u':
      if (span_equals(start, len, "union")) return UNION_TOK;
      if (span_equals(start, len, "unsigned")) return UNSIGNED_TOK;
      break;
    case 'v':
      if (span_equals(start, len, "void")) return VOID_TOK;
      if (span_equals(start, len, "volatile")) return VOLATILE_TOK;
      break;
    case 'w':
      if (span_equals(start, len, "while")) return WHILE_TOK;
      break;
  }
  return IDENT;
}

// Consume an identifier or keyword in a single scan. Identifier slices point
// into the source buffer; keywords do not allocate an unused slice.
static bool consume_identifier_or_keyword(struct Token* token) {
  skip();
  if (isalpha((unsigned char)*current) || *current == '_') {
    char const * start = current;
    do {
      current += 1;
    } while(isalnum((unsigned char)*current) || *current == '_');

    size_t len = (size_t)(current - start);
    token->type = classify_identifier(start, len);
    token->start = start;
    if (token->type == IDENT) {
      struct Slice* slice = token_array_new_slice(lex_tokens, start, len);
      if (slice == NULL) {
        fprintf(stderr,
                "Lexer memory error: unable to allocate identifier slice at "
                "%s:%zu:%zu\n",
                source_filename_for_ptr(start),
                source_location_from_ptr(start).line,
                source_location_from_ptr(start).column);
        lexer_failed = true;
        return false;
      }
      token->data.ident_name = slice;
    }
    return true;
  } else {
    return false;
  }
}

// Consume a decimal or hex integer literal token.
// Returns true on success and advances current past the literal.
// Supports optional u/U and l/L suffixes; accepts .0
// fractional forms as integer literals for integer-only parsing.
static bool consume_literal(struct Token* token) {
  skip();
  if (isdigit((unsigned char)*current)) {
    // number literal
    char const * start = current;
    uint64_t v = 0;
    bool is_hex = false;
    if (*current == '0' &&
        (current[1] == 'x' || current[1] == 'X') &&
        isxdigit((unsigned char)current[2])) {
      current += 2;
      is_hex = true;
      while (isxdigit((unsigned char)*current)) {
        int digit = hex_digit_value((unsigned char)*current);
        v = (v * 16) + (uint64_t)digit;
        current += 1;
      }
    } else {
      do {
        v = 10*v + (uint64_t)((*current) - '0');
        current += 1;
      } while (isdigit((unsigned char)*current));
    }

    bool saw_u = false;
    bool saw_l = false;
    for (int i = 0; i < 2; i++) {
      if ((*current == 'u' || *current == 'U') && !saw_u) {
        saw_u = true;
        current += 1;
        continue;
      }
      if ((*current == 'l' || *current == 'L') && !saw_l) {
        saw_l = true;
        current += 1;
        continue;
      }
      break;
    }

    if (saw_u && saw_l) {
      token->type = U_LONG_LIT;
      token->data.ulong_val = (unsigned long)v;
    } else if (saw_l) {
      token->type = LONG_LIT;
      token->data.long_val = (long)v;
    } else if (saw_u) {
      if (v <= kUIntMax) {
        token->type = U_INT_LIT;
        token->data.uint_val = (unsigned)v;
      } else {
        token->type = U_LONG_LIT;
        token->data.ulong_val = (unsigned long)v;
      }
    } else if (is_hex) {
      if (v <= kIntMax) {
        token->type = INT_LIT;
        token->data.int_val = (int)v;
      } else if (v <= kUIntMax) {
        token->type = U_INT_LIT;
        token->data.uint_val = (unsigned)v;
      } else if (v <= kLongMax) {
        token->type = LONG_LIT;
        token->data.long_val = (long)v;
      } else {
        token->type = U_LONG_LIT;
        token->data.ulong_val = (unsigned long)v;
      }
    } else {
      if (v <= kIntMax) {
        token->type = INT_LIT;
        token->data.int_val = (int)v;
      } else if (v <= kLongMax) {
        token->type = LONG_LIT;
        token->data.long_val = (long)v;
      } else {
        token->type = U_LONG_LIT;
        token->data.ulong_val = (unsigned long)v;
      }
    }
    token->start = start;
    return true;
  } else if (*current == '\'') {
    const char* start = current;

    // char literal
    consume("\'");

    if (*current == '\0' || *current == '\n') {
      print_error();
      lexer_failed = true;
      return false;
    }

    // detect escape characters
    if (*current == '\\'){
      if (*(current + 1) == 'x') {
        size_t digits = 0;
        unsigned char value = 0;
        if (!decode_hex_escape(current + 2, &digits, &value)) {
          if (digits == 0) {
            print_error_at(current + 1,
                           "expected at least one hexadecimal digit after \\x");
          } else {
            print_error_at(current + 2 + digits,
                           "hex escape exceeds byte value 0xff");
          }
          lexer_failed = true;
          return false;
        }
        token->data.char_val = (char)value;
        current += 2 + digits;
      } else {
        switch (*(current + 1)){
          case '\'':
            token->data.char_val = '\'';
            break;
          case '\"':
            token->data.char_val = '\"';
            break;
          case '\?':
            token->data.char_val = '\?';
            break;
          case '\\':
            token->data.char_val = '\\';
            break;
          case 'a':
            token->data.char_val = '\a';
            break;
          case 'b':
            token->data.char_val = '\b';
            break;
          case 'f':
            token->data.char_val = '\f';
            break;
          case 'n':
            token->data.char_val = '\n';
            break;
          case 'r':
            token->data.char_val = '\r';
            break;
          case 't':
            token->data.char_val = '\t';
            break;
          case 'v':
            token->data.char_val = '\v';
            break;
          case '0':
            token->data.char_val = '\0';
            break;
          default:
            print_error();
            lexer_failed = true;
            return false;
        }

        current += 2;
      }
    } else {
      if (*current == '\'') {
        print_error();
        lexer_failed = true;
        return false;
      }
      token->data.char_val = *current;
      current += 1;
    }

    if (!consume("\'")){
      print_error();
      lexer_failed = true;
      return false;
    }

    token->start = start;
    token->type = CHAR_LIT;
    return true;
  } else if (*current == '\"') {
    const char* start = current;

    // string literal
    current += 1;
    bool escaped = false;
    while ((*current != '\"' || *(current - 1) == '\\') && *current != '\0') {
      if (*current == '\n') {
        print_error();
        lexer_failed = true;
        return false;
      }
      if (escaped){
        switch (*current){
          case '\'':
          case '\"':
          case '\?':
          case '\\':
          case 'a':
          case 'b':
          case 'f':
          case 'n':
          case 'r':
          case 't':
          case 'v':
          case '0':
            // allowed escapes
            break;
          case 'x':
            if (hex_digit_value(*(current + 1)) < 0) {
              print_error_at(current,
                             "expected at least one hexadecimal digit after \\x");
              lexer_failed = true;
              return false;
            }
            break;
          default:
            // unrecognized escape
            print_error();
            lexer_failed = true;
            return false;
        }
      }

      if (!escaped && *current == '\\') escaped = true;
      else escaped = false;
      current++;
    }

    if (*current != '\"'){
      print_error();
      lexer_failed = true;
      return false;
    }

    current++;

    struct Slice* slice = token_array_new_slice(lex_tokens, start + 1,
                                                (size_t)(current - start - 2));
    if (slice == NULL) {
      fprintf(stderr,
              "Lexer memory error: unable to allocate string literal slice at "
              "%s:%zu:%zu\n",
              source_filename_for_ptr(start),
              source_location_from_ptr(start).line,
              source_location_from_ptr(start).column);
      lexer_failed = true;
      return false;
    }
    token->data.string_val = slice;
    token->start = start;
    token->type = STRING_LIT;
    return true;

  } else {
    return false;
  }
}

// Consume a punctuation token of length len at the current cursor.
static struct Token* finish_punctuation(struct Token* token,
                                        enum TokenType type,
                                        size_t len) {
  token->type = type;
  token->start = current;
  current += len;
  return token;
}

// Recognize punctuation by its first byte, checking multi-byte operators
// longest-first. Returns NULL without advancing when current is not punctuation.
static struct Token* consume_punctuation(struct Token* token) {
  switch (current[0]) {
    case '.': return finish_punctuation(token, DOT_TOK, 1);
    case ',': return finish_punctuation(token, COMMA, 1);
    case '?': return finish_punctuation(token, QUESTION, 1);
    case ':': return finish_punctuation(token, COLON, 1);
    case ';': return finish_punctuation(token, SEMI, 1);
    case '(': return finish_punctuation(token, OPEN_P, 1);
    case ')': return finish_punctuation(token, CLOSE_P, 1);
    case '{': return finish_punctuation(token, OPEN_B, 1);
    case '}': return finish_punctuation(token, CLOSE_B, 1);
    case '[': return finish_punctuation(token, OPEN_S, 1);
    case ']': return finish_punctuation(token, CLOSE_S, 1);
    case '~': return finish_punctuation(token, TILDE, 1);
    case '+':
      if (current[1] == '+') return finish_punctuation(token, INC_TOK, 2);
      if (current[1] == '=') return finish_punctuation(token, PLUS_EQ, 2);
      return finish_punctuation(token, PLUS, 1);
    case '-':
      if (current[1] == '>') return finish_punctuation(token, ARROW_TOK, 2);
      if (current[1] == '-') return finish_punctuation(token, DEC_TOK, 2);
      if (current[1] == '=') return finish_punctuation(token, MINUS_EQ, 2);
      return finish_punctuation(token, MINUS, 1);
    case '*':
      if (current[1] == '=') return finish_punctuation(token, TIMES_EQ, 2);
      return finish_punctuation(token, ASTERISK, 1);
    case '/':
      if (current[1] == '=') return finish_punctuation(token, DIV_EQ, 2);
      return finish_punctuation(token, SLASH, 1);
    case '%':
      if (current[1] == '=') return finish_punctuation(token, MOD_EQ, 2);
      return finish_punctuation(token, PERCENT, 1);
    case '&':
      if (current[1] == '&') return finish_punctuation(token, DOUBLE_AMPERSAND, 2);
      if (current[1] == '=') return finish_punctuation(token, AND_EQ, 2);
      return finish_punctuation(token, AMPERSAND, 1);
    case '|':
      if (current[1] == '|') return finish_punctuation(token, DOUBLE_PIPE, 2);
      if (current[1] == '=') return finish_punctuation(token, OR_EQ, 2);
      return finish_punctuation(token, PIPE, 1);
    case '^':
      if (current[1] == '=') return finish_punctuation(token, XOR_EQ, 2);
      return finish_punctuation(token, CARAT, 1);
    case '>':
      if (current[1] == '>') {
        if (current[2] == '=') return finish_punctuation(token, SHR_EQ, 3);
        return finish_punctuation(token, SHIFT_R_TOK, 2);
      }
      if (current[1] == '=') return finish_punctuation(token, GREATER_THAN_EQ, 2);
      return finish_punctuation(token, GREATER_THAN, 1);
    case '<':
      if (current[1] == '<') {
        if (current[2] == '=') return finish_punctuation(token, SHL_EQ, 3);
        return finish_punctuation(token, SHIFT_L_TOK, 2);
      }
      if (current[1] == '=') return finish_punctuation(token, LESS_THAN_EQ, 2);
      return finish_punctuation(token, LESS_THAN, 1);
    case '!':
      if (current[1] == '=') return finish_punctuation(token, NOT_EQUAL, 2);
      return finish_punctuation(token, EXCLAMATION, 1);
    case '=':
      if (current[1] == '=') return finish_punctuation(token, DOUBLE_EQUALS, 2);
      return finish_punctuation(token, EQUALS, 1);
    default:
      return NULL;
  }
}

// Consume the next available token into *token.
// Returns false if no token matches (end of input or a lexical error).
static bool consume_any(struct Token* token){
  skip();
  if (consume_identifier_or_keyword(token)) return true;
  if (consume_punctuation(token) != NULL) return true;
  return consume_literal(token);
}

// Tokenize a preprocessed source buffer into a TokenArray.
// prog is the NUL-terminated source buffer to lex.
// Returns a TokenArray or NULL on error.
// prog remains valid for the lifetime of token slices.
struct TokenArray* lex(char* prog){
  program = prog;
  current = prog;
  lexer_failed = false;

  struct TokenArray* result = create_token_array(1000);
  lex_tokens = result;

  struct Token current_token;
  while (consume_any(&current_token)){
    token_array_append(result, &current_token);
  }
  lex_tokens = NULL;

  if (lexer_failed) {
    destroy_token_array(result);
    return NULL;
  }

  if (!is_at_end()) {
    destroy_token_array(result);
    print_error();
    return NULL;
  }

  token_array_shrink(result);
  return result;
}
