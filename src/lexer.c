#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

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
// Define numeric literal bounds for target integer sizes.
static const uint64_t kIntBits = 32;
static const uint64_t kLongBits = 64;
static const uint64_t kIntMax = (UINT64_C(1) << (kIntBits - 1)) - 1;
static const uint64_t kUIntMax = (UINT64_C(1) << kIntBits) - 1;
static const uint64_t kLongMax = (UINT64_C(1) << (kLongBits - 1)) - 1;
static char const * last_token_start;
static size_t last_token_len;
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

// Consume a fixed string token at the current cursor.
// Returns true on match and updates last_token_* metadata.
static bool consume(const char* str) {
  skip();
  const char* start = current;
  size_t i = 0;
  while (true) {
    char const expected = str[i];
    char const found = current[i];
    if (expected == 0) {
      /* survived to the end of the expected string */
      current += i;
      last_token_start = start;
      last_token_len = i;
      return true;
    }
    if (expected != found) {
      return false;
    }
    // assertion: found != 0
    i += 1;
  } 
}

// Consume a keyword token, enforcing a word boundary.
// Returns true on match and updates last_token_* metadata.
static bool consume_keyword(const char* str) {
  skip();
  const char* start = current;
  size_t i = 0;
  while (true) {
    char const expected = str[i];
    char const found = current[i];
    if (expected == 0) {
      /* survived to the end of the expected string */
      if (!isalnum((unsigned char)found) && found != '_') {
        // word break
        current += i;
        last_token_start = start;
        last_token_len = i;
        return true;
      } else {
        return false;
      }
    }
    if (expected != found) {
      return false;
    }
    // assertion: found != 0
    i += 1;
  } 
}

// Consume an identifier token and allocate its slice.
// Returns true on success and advances current past the identifier.
// Identifier slices point into the source buffer.
static bool consume_identifier(struct Token* token) {
  skip();
  if (isalpha((unsigned char)*current) || *current == '_') {
    char const * start = current;
    do {
      current += 1;
    } while(isalnum((unsigned char)*current) || *current == '_');

    struct Slice* slice = malloc(sizeof(struct Slice));
    slice->start = start;
    slice->len = (current - start);

    token->type = IDENT;
    token->data.ident_name = slice;
    token->start = start;
    token->len = slice->len;
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
    token->len = (size_t)(current - start);
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
    token->len = (size_t)(current - start);
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

    struct Slice* slice = malloc(sizeof(struct Slice));
    slice->len = (size_t)(current - start - 2);
    slice->start = start + 1;
    token->data.string_val = slice;
    token->start = start;
    token->type = STRING_LIT;
    token->len = (size_t)(current - start);
    return true;

  } else {
    return false;
  }
}

// Finalize a token that was consumed via a fixed string match.
// Returns token after populating its type/start/len fields.
static struct Token* finish_simple_token(struct Token* token, enum TokenType type) {
  token->type = type;
  token->start = last_token_start;
  token->len = last_token_len;
  return token;
}

// Consume the next available token.
// Returns a heap-allocated Token or NULL if no token matches.
static struct Token* consume_any(){
  struct Token* token = malloc(sizeof(struct Token));

  if (consume_keyword("return")) return finish_simple_token(token, RETURN_TOK);
  if (consume_keyword("void")) return finish_simple_token(token, VOID_TOK);
  if (consume_keyword("if")) return finish_simple_token(token, IF_TOK);
  if (consume_keyword("else")) return finish_simple_token(token, ELSE_TOK);
  if (consume_keyword("do")) return finish_simple_token(token, DO_TOK);
  if (consume_keyword("while")) return finish_simple_token(token, WHILE_TOK);
  if (consume_keyword("for")) return finish_simple_token(token, FOR_TOK);
  if (consume_keyword("goto")) return finish_simple_token(token, GOTO_TOK);
  if (consume_keyword("break")) return finish_simple_token(token, BREAK_TOK);
  if (consume_keyword("continue")) return finish_simple_token(token, CONTINUE_TOK);
  if (consume_keyword("static")) return finish_simple_token(token, STATIC_TOK);
  if (consume_keyword("extern")) return finish_simple_token(token, EXTERN_TOK);
  if (consume_keyword("const")) return finish_simple_token(token, CONST_TOK);
  if (consume_keyword("switch")) return finish_simple_token(token, SWITCH_TOK);
  if (consume_keyword("case")) return finish_simple_token(token, CASE_TOK);
  if (consume_keyword("default")) return finish_simple_token(token, DEFAULT_TOK);
  if (consume_keyword("int")) return finish_simple_token(token, INT_TOK);
  if (consume_keyword("unsigned")) return finish_simple_token(token, UNSIGNED_TOK);
  if (consume_keyword("signed")) return finish_simple_token(token, SIGNED_TOK);
  if (consume_keyword("long")) return finish_simple_token(token, LONG_TOK);
  if (consume_keyword("short")) return finish_simple_token(token, SHORT_TOK);
  if (consume_keyword("char")) return finish_simple_token(token, CHAR_TOK);
  if (consume_keyword("sizeof")) return finish_simple_token(token, SIZEOF_TOK);
  if (consume_keyword("__attribute__")) return finish_simple_token(token, ATTRIBUTE_TOK);
  if (consume_keyword("struct")) return finish_simple_token(token, STRUCT_TOK);
  if (consume_keyword("union")) return finish_simple_token(token, UNION_TOK);
  if (consume_keyword("enum")) return finish_simple_token(token, ENUM_TOK);

  if (consume(".")) return finish_simple_token(token, DOT_TOK);
  if (consume("->")) return finish_simple_token(token, ARROW_TOK);
  if (consume(",")) return finish_simple_token(token, COMMA);
  if (consume("?")) return finish_simple_token(token, QUESTION);
  if (consume(":")) return finish_simple_token(token, COLON);
  if (consume(";")) return finish_simple_token(token, SEMI);
  if (consume("(")) return finish_simple_token(token, OPEN_P);
  if (consume(")")) return finish_simple_token(token, CLOSE_P);
  if (consume("{")) return finish_simple_token(token, OPEN_B);
  if (consume("}")) return finish_simple_token(token, CLOSE_B);
  if (consume("[")) return finish_simple_token(token, OPEN_S);
  if (consume("]")) return finish_simple_token(token, CLOSE_S);
  if (consume("~")) return finish_simple_token(token, TILDE);
  if (consume("++")) return finish_simple_token(token, INC_TOK);
  if (consume("--")) return finish_simple_token(token, DEC_TOK);
  if (consume("+=")) return finish_simple_token(token, PLUS_EQ);
  if (consume("-=")) return finish_simple_token(token, MINUS_EQ);
  if (consume("*=")) return finish_simple_token(token, TIMES_EQ);
  if (consume("/=")) return finish_simple_token(token, DIV_EQ);
  if (consume("%=")) return finish_simple_token(token, MOD_EQ);
  if (consume("+")) return finish_simple_token(token, PLUS);
  if (consume("-")) return finish_simple_token(token, MINUS);
  if (consume("*")) return finish_simple_token(token, ASTERISK);
  if (consume("/")) return finish_simple_token(token, SLASH);
  if (consume("%")) return finish_simple_token(token, PERCENT);
  if (consume("&&")) return finish_simple_token(token, DOUBLE_AMPERSAND);
  if (consume("||")) return finish_simple_token(token, DOUBLE_PIPE);
  if (consume("&=")) return finish_simple_token(token, AND_EQ);
  if (consume("|=")) return finish_simple_token(token, OR_EQ);
  if (consume("^=")) return finish_simple_token(token, XOR_EQ);
  if (consume(">>=")) return finish_simple_token(token, SHR_EQ);
  if (consume("<<=")) return finish_simple_token(token, SHL_EQ);
  if (consume("&")) return finish_simple_token(token, AMPERSAND);
  if (consume("|")) return finish_simple_token(token, PIPE);
  if (consume("^")) return finish_simple_token(token, CARAT);
  if (consume(">>")) return finish_simple_token(token, SHIFT_R_TOK);
  if (consume("<<")) return finish_simple_token(token, SHIFT_L_TOK);
  if (consume("!=")) return finish_simple_token(token, NOT_EQUAL);
  if (consume("!")) return finish_simple_token(token, EXCLAMATION);
  if (consume("==")) return finish_simple_token(token, DOUBLE_EQUALS);
  if (consume(">=")) return finish_simple_token(token, GREATER_THAN_EQ);
  if (consume("<=")) return finish_simple_token(token, LESS_THAN_EQ);
  if (consume("=")) return finish_simple_token(token, EQUALS);
  if (consume(">")) return finish_simple_token(token, GREATER_THAN);
  if (consume("<")) return finish_simple_token(token, LESS_THAN);

  if (consume_identifier(token)) return token;
  if (consume_literal(token)) return token;

  free(token);
  return NULL;
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

  struct Token* current_token = consume_any();
  while (current_token != NULL){    
    token_array_append(result, current_token);
    current_token = consume_any();
  }

  if (lexer_failed) {
    destroy_token_array(result);
    return NULL;
  }

  if (!is_at_end()) {
    destroy_token_array(result);
    print_error();
    return NULL;
  }

  return result;
}
