#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "slice.h"
#include "token.h"
#include "token_array.h"
#include "lexer.h"
#include "source_location.h"

// Purpose: Tokenize preprocessed source into a stream of tokens.
// Inputs: Uses the global cursor to walk the NUL-terminated source buffer.
// Outputs: Produces Token structures appended to a TokenArray.
// Invariants/Assumptions: Whitespace is skipped; no comments remain (preprocessed).

static char const * program;
static char const * current;
// Purpose: Define numeric literal bounds for target integer sizes.
// Inputs/Outputs: Used to classify integer literal token types.
// Invariants/Assumptions: int is 32-bit and long is 64-bit in the target model.
static const uint64_t kIntBits = 32;
static const uint64_t kLongBits = 64;
static const uint64_t kIntMax = (UINT64_C(1) << (kIntBits - 1)) - 1;
static const uint64_t kUIntMax = (UINT64_C(1) << kIntBits) - 1;
static const uint64_t kLongMax = (UINT64_C(1) << (kLongBits - 1)) - 1;
static char const * last_token_start;
static size_t last_token_len;

// Purpose: Emit a lexer error diagnostic for the current cursor.
// Inputs: current points to the byte where tokenization failed.
// Outputs: Writes an error message to stdout.
// Invariants/Assumptions: source context has been initialized for locations.
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

// Purpose: Determine whether the lexer has reached the end of input.
// Inputs: Uses the global cursor and skips trailing whitespace.
// Outputs: Returns true when no more non-space characters remain.
// Invariants/Assumptions: current points into the same buffer as program.
static bool is_at_end() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
  if (*current != 0) return false;
  else return true;
}

// Purpose: Skip over ASCII whitespace characters.
// Inputs: Uses the global cursor pointer.
// Outputs: Advances current past whitespace.
// Invariants/Assumptions: Whitespace is not emitted as tokens.
static void skip() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
}

// Purpose: Consume a fixed string token at the current cursor.
// Inputs: str is the literal to match.
// Outputs: Returns true on match and updates last_token_* metadata.
// Invariants/Assumptions: Skips leading whitespace before matching.
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

// Purpose: Consume a keyword token, enforcing a word boundary.
// Inputs: str is the keyword literal to match.
// Outputs: Returns true on match and updates last_token_* metadata.
// Invariants/Assumptions: Rejects identifiers that merely prefix the keyword.
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
        // this is actually an identifier
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

// Purpose: Consume an identifier token and allocate its slice.
// Inputs: token is the destination Token to populate.
// Outputs: Returns true on success and advances current past the identifier.
// Invariants/Assumptions: Identifier slices point into the source buffer.
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

// Purpose: Consume a decimal or hex integer literal token.
// Inputs: token is the destination Token to populate.
// Outputs: Returns true on success and advances current past the literal.
// Invariants/Assumptions: Supports optional u/U and l/L suffixes.
// Purpose: Convert a hex digit character into its numeric value.
// Inputs: ch is an ASCII hex digit.
// Outputs: Returns the digit value or -1 for non-hex characters.
// Invariants/Assumptions: Caller checks isxdigit before using this.
static int hex_digit_value(int ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

static bool consume_literal(struct Token* token) {
  skip();
  if (isdigit((unsigned char)*current)) {
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
  } else {
    return false;
  }
}

// Purpose: Finalize a token that was consumed via a fixed string match.
// Inputs: token is the allocated Token; type is the token type to assign.
// Outputs: Returns token after populating its type/start/len fields.
// Invariants/Assumptions: last_token_* was set by consume/consume_keyword.
static struct Token* finish_simple_token(struct Token* token, enum TokenType type) {
  token->type = type;
  token->start = last_token_start;
  token->len = last_token_len;
  return token;
}

// Purpose: Consume the next available token.
// Inputs: Uses the global cursor to scan the next token.
// Outputs: Returns a heap-allocated Token or NULL if no token matches.
// Invariants/Assumptions: Caller frees the returned token when destroying arrays.
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
  if (consume_keyword("switch")) return finish_simple_token(token, SWITCH_TOK);
  if (consume_keyword("case")) return finish_simple_token(token, CASE_TOK);
  if (consume_keyword("default")) return finish_simple_token(token, DEFAULT_TOK);
  if (consume_keyword("int")) return finish_simple_token(token, INT_TOK);
  if (consume_keyword("unsigned")) return finish_simple_token(token, UNSIGNED_TOK);
  if (consume_keyword("signed")) return finish_simple_token(token, SIGNED_TOK);
  if (consume_keyword("long")) return finish_simple_token(token, LONG_TOK);

  if (consume(",")) return finish_simple_token(token, COMMA);
  if (consume("?")) return finish_simple_token(token, QUESTION);
  if (consume(":")) return finish_simple_token(token, COLON);
  if (consume(";")) return finish_simple_token(token, SEMI);
  if (consume("(")) return finish_simple_token(token, OPEN_P);
  if (consume(")")) return finish_simple_token(token, CLOSE_P);
  if (consume("{")) return finish_simple_token(token, OPEN_B);
  if (consume("}")) return finish_simple_token(token, CLOSE_B);
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

// Purpose: Tokenize a preprocessed source buffer into a TokenArray.
// Inputs: prog is the NUL-terminated source buffer to lex.
// Outputs: Returns a TokenArray or NULL on error.
// Invariants/Assumptions: prog remains valid for the lifetime of token slices.
struct TokenArray* lex(char* prog){
  program = prog;
  current = prog;

  struct TokenArray* result = create_token_array(1000);

  struct Token* current_token = consume_any();
  while (current_token != NULL){    
    token_array_append(result, current_token);
    current_token = consume_any();
  }

  if (!is_at_end()) {
    destroy_token_array(result);
    print_error();
    return NULL;
  }

  return result;
}
