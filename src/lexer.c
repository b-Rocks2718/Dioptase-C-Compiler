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

static char const * program;
static char const * current;
static char const * last_token_start;
static size_t last_token_len;

static void print_error() {
  struct SourceLocation loc = source_location_from_ptr(current);
  const char* filename = source_filename();
  if (*current == '\0') {
    printf("Lexer error at %s:%zu:%zu: unexpected end of input\n",
           filename, loc.line, loc.column);
    return;
  }
  printf("Lexer error at %s:%zu:%zu: unexpected character '%c'\n",
         filename, loc.line, loc.column, *current);
  printf("%s\n", current);
}

static bool is_at_end() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
  if (*current != 0) return false;
  else return true;
}

static void skip() {
  while (isspace((unsigned char)*current)) {
    current += 1;
  }
}

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

static bool consume_literal(struct Token* token) {
  skip();
  if (isdigit((unsigned char)*current)) {
    char const * start = current;
    uint64_t v = 0;
    do {
      v = 10*v + ((*current) - '0');
      current += 1;
    } while (isdigit((unsigned char)*current));

    token->type = INT_LIT;
    token->data.int_val = v;
    token->start = start;
    token->len = (size_t)(current - start);
    return true;
  } else {
    return false;
  }
}

static struct Token* finish_simple_token(struct Token* token, enum TokenType type) {
  token->type = type;
  token->start = last_token_start;
  token->len = last_token_len;
  return token;
}

// consumes any token
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
