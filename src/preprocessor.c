#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "preprocessor.h"

// copy the program into a new string, but without the comments
char * preprocess(char const* prog){
  size_t prog_index = 0;
  size_t result_index = 0;

  size_t capacity = 5; // using a dynamic array here
  char * result = malloc(sizeof(char) * capacity);

  bool in_string = false;
  bool in_char = false;
  bool escape = false;

  while (prog[prog_index] != 0){
    if (!in_string && !in_char) {
      // remove single line // comments
      if (prog[prog_index] == '/' && prog[prog_index + 1] == '/') {
        if (result_index == capacity - 1) { // leave room for null terminator
          result = realloc(result, 2 * capacity);
          capacity = 2 * capacity;
        }
        result[result_index++] = ' ';

        prog_index += 2;
        while (prog[prog_index] != '\0' && prog[prog_index] != '\n') {
          prog_index++;
        }
        if (prog[prog_index] == '\n') {
          if (result_index == capacity - 1) {
            result = realloc(result, 2 * capacity);
            capacity = 2 * capacity;
          }
          result[result_index++] = '\n';
          prog_index++;
        }
        continue;
      }

      // remove multi line /* */ comments
      if (prog[prog_index] == '/' && prog[prog_index + 1] == '*') {
        if (result_index == capacity - 1) {
          result = realloc(result, 2 * capacity);
          capacity = 2 * capacity;
        }
        result[result_index++] = ' ';

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

    if (result_index == capacity - 1) { // leave room for null terminator
      result = realloc(result, 2 * capacity);
      capacity = 2 * capacity;
    }
    result[result_index++] = prog[prog_index];
    prog_index++;
  }

  // include null terminator
  result[result_index] = 0;

  return result;
}
