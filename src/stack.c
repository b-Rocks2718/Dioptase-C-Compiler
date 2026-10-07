#include "stack.h"
#include "exit_codes.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Terminate when this internal analysis helper cannot preserve stack state.
static void stack_allocation_error(const char* operation, int capacity) {
  fprintf(stderr,
          "integer stack: unable to %s storage for %d elements\n",
          operation, capacity);
  exit(BCC_EXIT_INTERNAL);
}

// Compute the backing allocation size without overflowing size_t.
static size_t stack_allocation_size(int capacity) {
  if ((size_t)capacity > SIZE_MAX / sizeof(int)) {
    stack_allocation_error("represent", capacity);
  }
  return (size_t)capacity * sizeof(int);
}

// Create a new stack with the given capacity.
struct Stack* create_stack(int capacity) {
  if (capacity < 1) {
    capacity = 1;
  }
  struct Stack* stack = (struct Stack*)malloc(sizeof(struct Stack));
  if (stack == NULL) {
    stack_allocation_error("allocate", capacity);
  }
  stack->data = (int*)malloc(stack_allocation_size(capacity));
  if (stack->data == NULL) {
    free(stack);
    stack_allocation_error("allocate", capacity);
  }
  stack->capacity = capacity;
  stack->size = 0;
  return stack;
}

// Free the memory allocated for the stack.
void free_stack(struct Stack* stack) {
  if (stack) {
    free(stack->data);
    free(stack);
  }
}

// Push a value onto the stack.
void stack_push(struct Stack* stack, int value) {
  if (stack->size >= stack->capacity) {
    if (stack->capacity > INT_MAX / 2) {
      stack_allocation_error("grow", stack->capacity);
    }
    int new_capacity = stack->capacity * 2;
    int* new_data =
        (int*)realloc(stack->data, stack_allocation_size(new_capacity));
    if (new_data == NULL) {
      stack_allocation_error("grow", new_capacity);
    }
    stack->data = new_data;
    stack->capacity = new_capacity;
  }
  stack->data[stack->size++] = value;
}

// Pop a value from the stack. Returns the popped value. 
// Undefined behavior if the stack is empty.
int stack_pop(struct Stack* stack) {
  return stack->data[--stack->size];
}

// Get the value at the top of the stack without popping it.
// Undefined behavior if the stack is empty.
int stack_top(const struct Stack* stack) {
  return stack->data[stack->size - 1];
}

// Get the number of elements currently in the stack.
int stack_size(const struct Stack* stack) {
  return stack->size;
}
