#include "stack.h"

#include <stdlib.h> 


// Create a new stack with the given capacity.
struct Stack* create_stack(int capacity) {
  struct Stack* stack = (struct Stack*)malloc(sizeof(struct Stack));
  stack->data = (int*)malloc(sizeof(int) * capacity);
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
    stack->capacity *= 2;
    stack->data = (int*)realloc(stack->data, sizeof(int) * stack->capacity);
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
