#ifndef STACK_H
#define STACK_H

// Simple stack implementation for int
// will grow dynamically as needed, but not shrink automatically.
struct Stack {
  int* data;
  int capacity;
  int size;
};

// Create a new stack with the given capacity.
struct Stack* create_stack(int capacity);

// Free the memory allocated for the stack.
void free_stack(struct Stack* stack);

// Push a value onto the stack.
void stack_push(struct Stack* stack, int value);

// Pop a value from the stack. Returns the popped value. 
// Undefined behavior if the stack is empty.
int stack_pop(struct Stack* stack);

// Get the value at the top of the stack without popping it.
// Undefined behavior if the stack is empty.
int stack_top(const struct Stack* stack);

// Get the number of elements currently in the stack.
int stack_size(const struct Stack* stack);

#endif // STACK_H