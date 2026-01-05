#include "TAC.h"
#include "slice.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Purpose: Provide a small TAC interpreter for validating TAC lowering output.
// Inputs: Consumes a TACProg with functions and static variables.
// Outputs: Returns the integer result of main() or exits on interpreter errors.
// Invariants/Assumptions: All values are 32-bit ints; memory is byte-addressed.

// Purpose: Define interpreter memory sizing constants.
// Inputs: Used for address allocation and dynamic array growth.
// Outputs: Controls initial capacities, growth behavior, and scalar slot counts.
// Invariants/Assumptions: Values are stored in 4-byte slots and grow by factor 2.
static const int kTacInterpWordBytes = 4;
static const size_t kTacInterpInitialMemoryCapacity = 8;
static const size_t kTacInterpInitialBindingCapacity = 8;
static const size_t kTacInterpInitialLabelCapacity = 8;
static const size_t kTacInterpGrowthFactor = 2;
static const size_t kTacInterpSingleSlot = 1;
static const size_t kTacInterpMainNameLen = sizeof("main") - 1;

// Purpose: Track one addressable memory cell in the interpreter.
// Inputs: address is a byte address; value is the stored integer.
// Outputs: initialized indicates whether the cell has a defined value.
// Invariants/Assumptions: address values are byte addresses; allocations align to kTacInterpWordBytes.
struct TacMemoryCell {
  int address;
  int value;
  bool initialized;
};

// Purpose: Maintain the interpreter's memory space.
// Inputs: cells holds allocated memory slots; next_address is the allocator cursor.
// Outputs: Stores variable and pointer-referenced values.
// Invariants/Assumptions: Addresses are monotonically allocated.
struct TacMemory {
  struct TacMemoryCell* cells;
  size_t count;
  size_t capacity;
  int next_address;
};

// Purpose: Map an identifier name to a memory address.
// Inputs: name is the variable identifier; address is its storage base.
// Outputs: Used for variable lookup and address-of operations.
// Invariants/Assumptions: name pointers remain valid for the interpreter lifetime.
struct TacBinding {
  struct Slice* name;
  int address;
};

// Purpose: Maintain name-to-address bindings for a scope.
// Inputs: bindings stores the list of bound identifiers.
// Outputs: Provides lookup for globals and locals.
// Invariants/Assumptions: Names are unique within a binding set.
struct TacBindings {
  struct TacBinding* bindings;
  size_t count;
  size_t capacity;
};

// Purpose: Associate a label name with its instruction node.
// Inputs: label is the label name; instr points at the label instruction.
// Outputs: Supports TACJUMP and TACCOND_JUMP dispatch.
// Invariants/Assumptions: Label names are unique within a function body.
struct TacLabelEntry {
  struct Slice* label;
  struct TACInstr* instr;
};

// Purpose: Store per-function execution state for the interpreter.
// Inputs: locals holds the local bindings; labels index jump targets.
// Outputs: Tracks comparison state for TACCOND_JUMP.
// Invariants/Assumptions: cmp_valid is set only after a TACCMP.
struct TacFrame {
  struct TacBindings locals;
  struct TacLabelEntry* labels;
  size_t label_count;
  size_t label_capacity;
  bool cmp_valid;
  int cmp_left;
  int cmp_right;
};

// Purpose: Hold interpreter-wide state across function calls.
// Inputs: prog is the TAC program; memory and globals are initialized up front.
// Outputs: Provides global storage and shared memory space.
// Invariants/Assumptions: Globals are initialized before executing main.
struct TacInterpreter {
  const struct TACProg* prog;
  struct TacMemory memory;
  struct TacBindings globals;
};

// Purpose: Emit a TAC interpreter error and terminate execution.
// Inputs: fmt is a printf-style format string.
// Outputs: Writes to stderr and exits with non-zero status.
// Invariants/Assumptions: Used for irrecoverable interpreter errors.
static void tac_interp_error(const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "TAC Interpreter Error: ");
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

// Purpose: Initialize a TacMemory structure.
// Inputs: mem points to the memory object to initialize.
// Outputs: Resets memory tracking to an empty state.
// Invariants/Assumptions: mem is non-NULL.
static void tac_memory_init(struct TacMemory* mem) {
  mem->cells = NULL;
  mem->count = 0;
  mem->capacity = 0;
  mem->next_address = 0;
}

// Purpose: Release a TacMemory's allocated resources.
// Inputs: mem points to the memory object to free.
// Outputs: Frees allocated memory slots.
// Invariants/Assumptions: mem was initialized with tac_memory_init.
static void tac_memory_destroy(struct TacMemory* mem) {
  free(mem->cells);
  mem->cells = NULL;
  mem->count = 0;
  mem->capacity = 0;
  mem->next_address = 0;
}

// Purpose: Grow the TacMemory cell array if needed.
// Inputs: mem points to the memory object to grow.
// Outputs: Ensures capacity for at least one more cell.
// Invariants/Assumptions: mem is non-NULL.
static void tac_memory_reserve(struct TacMemory* mem) {
  if (mem->count < mem->capacity) {
    return;
  }
  size_t new_capacity = (mem->capacity == 0)
                            ? kTacInterpInitialMemoryCapacity
                            : mem->capacity * kTacInterpGrowthFactor;
  struct TacMemoryCell* next_cells =
      (struct TacMemoryCell*)realloc(mem->cells, new_capacity * sizeof(struct TacMemoryCell));
  if (next_cells == NULL) {
    tac_interp_error("memory allocation failed while growing TAC memory");
  }
  mem->cells = next_cells;
  mem->capacity = new_capacity;
}

// Purpose: Find an existing memory cell by address.
// Inputs: mem is the memory object; address is the byte address to find.
// Outputs: Returns the cell pointer or NULL if not found.
// Invariants/Assumptions: mem is initialized.
static struct TacMemoryCell* tac_memory_find_cell(struct TacMemory* mem, int address) {
  for (size_t i = 0; i < mem->count; i++) {
    if (mem->cells[i].address == address) {
      return &mem->cells[i];
    }
  }
  return NULL;
}

// Purpose: Ensure a memory cell exists for a given address.
// Inputs: mem is the memory object; address is the byte address to access.
// Outputs: Returns a pointer to the memory cell, creating it if missing.
// Invariants/Assumptions: New cells are marked uninitialized.
static struct TacMemoryCell* tac_memory_get_cell(struct TacMemory* mem, int address) {
  struct TacMemoryCell* cell = tac_memory_find_cell(mem, address);
  if (cell != NULL) {
    return cell;
  }
  tac_memory_reserve(mem);
  cell = &mem->cells[mem->count++];
  cell->address = address;
  cell->value = 0;
  cell->initialized = false;
  return cell;
}

// Purpose: Allocate a contiguous range of memory slots.
// Inputs: mem is the memory object; slots is the number of word slots to reserve.
// Outputs: Returns the base byte address for the allocated range.
// Invariants/Assumptions: slots must be non-zero.
static int tac_memory_alloc_range(struct TacMemory* mem, size_t slots) {
  if (slots == 0) {
    tac_interp_error("attempted to allocate zero TAC memory slots");
  }
  int base = mem->next_address;
  for (size_t i = 0; i < slots; i++) {
    int address = base + (int)(i * kTacInterpWordBytes);
    (void)tac_memory_get_cell(mem, address);
  }
  mem->next_address += (int)(slots * kTacInterpWordBytes);
  return base;
}

// Purpose: Store a value into a memory cell.
// Inputs: mem is the memory object; address is the target byte address; value is the data.
// Outputs: Writes the value and marks the cell initialized.
// Invariants/Assumptions: address refers to a word slot.
static void tac_memory_store(struct TacMemory* mem, int address, int value) {
  struct TacMemoryCell* cell = tac_memory_get_cell(mem, address);
  cell->value = value;
  cell->initialized = true;
}

// Purpose: Load a value from a memory cell.
// Inputs: mem is the memory object; address is the source byte address.
// Outputs: Returns the stored value.
// Invariants/Assumptions: Loading an uninitialized address is an error.
static int tac_memory_load(struct TacMemory* mem, int address) {
  struct TacMemoryCell* cell = tac_memory_find_cell(mem, address);
  if (cell == NULL || !cell->initialized) {
    tac_interp_error("load from uninitialized address %d", address);
  }
  return cell->value;
}

// Purpose: Initialize a TacBindings structure.
// Inputs: bindings points to the bindings object to initialize.
// Outputs: Resets the binding list to empty.
// Invariants/Assumptions: bindings is non-NULL.
static void tac_bindings_init(struct TacBindings* bindings) {
  bindings->bindings = NULL;
  bindings->count = 0;
  bindings->capacity = 0;
}

// Purpose: Release memory held by a TacBindings structure.
// Inputs: bindings points to the bindings object to free.
// Outputs: Frees the bindings array.
// Invariants/Assumptions: bindings was initialized with tac_bindings_init.
static void tac_bindings_destroy(struct TacBindings* bindings) {
  free(bindings->bindings);
  bindings->bindings = NULL;
  bindings->count = 0;
  bindings->capacity = 0;
}

// Purpose: Grow a binding list if needed.
// Inputs: bindings points to the bindings object to grow.
// Outputs: Ensures capacity for at least one more binding.
// Invariants/Assumptions: bindings is non-NULL.
static void tac_bindings_reserve(struct TacBindings* bindings) {
  if (bindings->count < bindings->capacity) {
    return;
  }
  size_t new_capacity = (bindings->capacity == 0)
                            ? kTacInterpInitialBindingCapacity
                            : bindings->capacity * kTacInterpGrowthFactor;
  struct TacBinding* next =
      (struct TacBinding*)realloc(bindings->bindings, new_capacity * sizeof(struct TacBinding));
  if (next == NULL) {
    tac_interp_error("memory allocation failed while growing TAC bindings");
  }
  bindings->bindings = next;
  bindings->capacity = new_capacity;
}

// Purpose: Find an existing binding by name.
// Inputs: bindings is the binding list; name is the identifier to find.
// Outputs: Returns the binding pointer or NULL if not found.
// Invariants/Assumptions: Name comparisons use slice equality.
static struct TacBinding* tac_bindings_find(struct TacBindings* bindings, const struct Slice* name) {
  for (size_t i = 0; i < bindings->count; i++) {
    if (compare_slice_to_slice(bindings->bindings[i].name, name)) {
      return &bindings->bindings[i];
    }
  }
  return NULL;
}

// Purpose: Create or return a binding for a name.
// Inputs: bindings is the binding list; mem is the shared memory allocator; name is the identifier.
// Outputs: Returns the binding for the identifier, allocating storage if missing.
// Invariants/Assumptions: Newly created bindings allocate one word slot.
static struct TacBinding* tac_bindings_get_or_add(struct TacBindings* bindings,
                                                  struct TacMemory* mem,
                                                  struct Slice* name) {
  struct TacBinding* found = tac_bindings_find(bindings, name);
  if (found != NULL) {
    return found;
  }
  tac_bindings_reserve(bindings);
  struct TacBinding* binding = &bindings->bindings[bindings->count++];
  binding->name = name;
  binding->address = tac_memory_alloc_range(mem, kTacInterpSingleSlot);
  return binding;
}

// Purpose: Select the binding list that should store a variable.
// Inputs: interp is the interpreter state; frame is the current frame; name is the identifier.
// Outputs: Returns the bindings list to use for the identifier.
// Invariants/Assumptions: Globals are matched before locals.
static struct TacBindings* tac_select_bindings(struct TacInterpreter* interp,
                                               struct TacFrame* frame,
                                               struct Slice* name) {
  if (tac_bindings_find(&interp->globals, name) != NULL) {
    return &interp->globals;
  }
  return &frame->locals;
}

// Purpose: Read a variable value by name.
// Inputs: interp is the interpreter state; frame is the current frame; name is the variable name.
// Outputs: Returns the stored integer value.
// Invariants/Assumptions: Reading an unbound or uninitialized variable is an error.
static int tac_read_var(struct TacInterpreter* interp, struct TacFrame* frame, struct Slice* name) {
  struct TacBinding* binding = tac_bindings_find(&interp->globals, name);
  if (binding == NULL) {
    binding = tac_bindings_find(&frame->locals, name);
  }
  if (binding == NULL) {
    tac_interp_error("read from unknown variable %.*s", (int)name->len, name->start);
  }
  return tac_memory_load(&interp->memory, binding->address);
}

// Purpose: Write a variable value by name, creating storage if needed.
// Inputs: interp is the interpreter state; frame is the current frame; name is the variable name.
// Outputs: Stores the value in memory.
// Invariants/Assumptions: Unknown globals are treated as locals.
static void tac_write_var(struct TacInterpreter* interp,
                          struct TacFrame* frame,
                          struct Slice* name,
                          int value) {
  struct TacBindings* bindings = tac_select_bindings(interp, frame, name);
  struct TacBinding* binding = tac_bindings_get_or_add(bindings, &interp->memory, name);
  tac_memory_store(&interp->memory, binding->address, value);
}

// Purpose: Resolve the address associated with a variable name.
// Inputs: interp is the interpreter state; frame is the current frame; name is the variable name.
// Outputs: Returns the address for the variable, allocating if needed.
// Invariants/Assumptions: Unknown globals are treated as locals.
static int tac_address_of(struct TacInterpreter* interp, struct TacFrame* frame, struct Slice* name) {
  struct TacBindings* bindings = tac_select_bindings(interp, frame, name);
  struct TacBinding* binding = tac_bindings_get_or_add(bindings, &interp->memory, name);
  return binding->address;
}

// Purpose: Evaluate a TAC value to its integer representation.
// Inputs: interp is the interpreter state; frame is the current frame; val is the TAC value.
// Outputs: Returns the integer representation of the value.
// Invariants/Assumptions: Variable values must be initialized before use.
static int tac_eval_val(struct TacInterpreter* interp, struct TacFrame* frame, const struct Val* val) {
  if (val == NULL) {
    tac_interp_error("attempted to evaluate a NULL TAC value");
  }
  switch (val->val_type) {
    case CONSTANT:
      return val->val.const_value;
    case VARIABLE:
      return tac_read_var(interp, frame, val->val.var_name);
    default:
      tac_interp_error("unknown TAC value type %d", (int)val->val_type);
      return 0;
  }
}

// Purpose: Assign a TAC value to a destination variable.
// Inputs: interp is the interpreter state; frame is the current frame; dst is the destination.
// Outputs: Writes the evaluated value into destination storage.
// Invariants/Assumptions: dst must be a VARIABLE value.
static void tac_assign_val(struct TacInterpreter* interp,
                           struct TacFrame* frame,
                           const struct Val* dst,
                           int value) {
  if (dst == NULL || dst->val_type != VARIABLE) {
    tac_interp_error("assignment target is not a variable");
  }
  tac_write_var(interp, frame, dst->val.var_name, value);
}

// Purpose: Determine the result of a TAC conditional jump.
// Inputs: cond is the condition enum; left/right are the compared values.
// Outputs: Returns true if the condition is satisfied.
// Invariants/Assumptions: Signed comparisons use int, unsigned use uint32_t.
static bool tac_condition_true(enum TACCondition cond, int left, int right) {
  switch (cond) {
    case CondE:
      return left == right;
    case CondNE:
      return left != right;
    case CondG:
      return left > right;
    case CondGE:
      return left >= right;
    case CondL:
      return left < right;
    case CondLE:
      return left <= right;
    case CondA:
      return (uint32_t)left > (uint32_t)right;
    case CondAE:
      return (uint32_t)left >= (uint32_t)right;
    case CondB:
      return (uint32_t)left < (uint32_t)right;
    case CondBE:
      return (uint32_t)left <= (uint32_t)right;
    default:
      tac_interp_error("unknown TAC condition %d", (int)cond);
      return false;
  }
}

// Purpose: Apply a unary operator to a value.
// Inputs: op is the unary operator; value is the operand.
// Outputs: Returns the result of the unary operation.
// Invariants/Assumptions: Boolean not returns 1 or 0.
static int tac_apply_unary(enum UnOp op, int value) {
  switch (op) {
    case COMPLEMENT:
      return ~value;
    case NEGATE:
      return -value;
    case BOOL_NOT:
      return value == 0 ? 1 : 0;
    default:
      tac_interp_error("unsupported unary operator %d", (int)op);
      return 0;
  }
}

// Purpose: Apply a binary operator to two values.
// Inputs: op is the binary operator; left/right are operands.
// Outputs: Returns the result of the binary operation.
// Invariants/Assumptions: Division or modulo by zero is an error.
static int tac_apply_binary(enum BinOp op, int left, int right) {
  switch (op) {
    case ADD_OP:
      return left + right;
    case SUB_OP:
      return left - right;
    case MUL_OP:
      return left * right;
    case DIV_OP:
      if (right == 0) {
        tac_interp_error("division by zero in TACBINARY");
      }
      return left / right;
    case MOD_OP:
      if (right == 0) {
        tac_interp_error("modulo by zero in TACBINARY");
      }
      return left % right;
    case BIT_AND:
      return left & right;
    case BIT_OR:
      return left | right;
    case BIT_XOR:
      return left ^ right;
    case BIT_SHR:
      return left >> right;
    case BIT_SHL:
      return left << right;
    case BOOL_EQ:
      return left == right ? 1 : 0;
    case BOOL_NEQ:
      return left != right ? 1 : 0;
    case BOOL_LE:
      return left < right ? 1 : 0;
    case BOOL_GE:
      return left > right ? 1 : 0;
    case BOOL_LEQ:
      return left <= right ? 1 : 0;
    case BOOL_GEQ:
      return left >= right ? 1 : 0;
    case BOOL_AND:
      return (left != 0 && right != 0) ? 1 : 0;
    case BOOL_OR:
      return (left != 0 || right != 0) ? 1 : 0;
    default:
      tac_interp_error("unsupported binary operator %d", (int)op);
      return 0;
  }
}

// Purpose: Initialize label metadata for a function frame.
// Inputs: frame is the frame to populate; body is the function's instruction list.
// Outputs: Populates frame->labels for quick label lookup.
// Invariants/Assumptions: Duplicate labels are rejected.
static void tac_collect_labels(struct TacFrame* frame, struct TACInstr* body) {
  for (struct TACInstr* cur = body; cur != NULL; cur = cur->next) {
    if (cur->type != TACLABEL) {
      continue;
    }
    struct Slice* label = cur->instr.tac_label.label;
    for (size_t i = 0; i < frame->label_count; i++) {
      if (compare_slice_to_slice(frame->labels[i].label, label)) {
        tac_interp_error("duplicate label %.*s in TAC function",
                         (int)label->len, label->start);
      }
    }
    if (frame->label_count == frame->label_capacity) {
      size_t new_capacity = (frame->label_capacity == 0)
                                ? kTacInterpInitialLabelCapacity
                                : frame->label_capacity * kTacInterpGrowthFactor;
      struct TacLabelEntry* next =
          (struct TacLabelEntry*)realloc(frame->labels, new_capacity * sizeof(struct TacLabelEntry));
      if (next == NULL) {
        tac_interp_error("memory allocation failed while building label table");
      }
      frame->labels = next;
      frame->label_capacity = new_capacity;
    }
    frame->labels[frame->label_count].label = label;
    frame->labels[frame->label_count].instr = cur;
    frame->label_count++;
  }
}

// Purpose: Look up a label instruction in the current frame.
// Inputs: frame is the current frame; label is the label name.
// Outputs: Returns the instruction pointer for the label.
// Invariants/Assumptions: Labels are collected before execution.
static struct TACInstr* tac_find_label(struct TacFrame* frame, struct Slice* label) {
  for (size_t i = 0; i < frame->label_count; i++) {
    if (compare_slice_to_slice(frame->labels[i].label, label)) {
      return frame->labels[i].instr;
    }
  }
  tac_interp_error("unknown label %.*s in TAC jump", (int)label->len, label->start);
  return NULL;
}

// Purpose: Initialize a function frame before execution.
// Inputs: frame is the frame to initialize; interp is the interpreter state.
// Outputs: Sets up local bindings and label table for the function.
// Invariants/Assumptions: Caller supplies the function body.
static void tac_frame_init(struct TacFrame* frame, struct TacInterpreter* interp, struct TACInstr* body) {
  tac_bindings_init(&frame->locals);
  frame->labels = NULL;
  frame->label_count = 0;
  frame->label_capacity = 0;
  frame->cmp_valid = false;
  frame->cmp_left = 0;
  frame->cmp_right = 0;
  tac_collect_labels(frame, body);
  (void)interp;
}

// Purpose: Release resources associated with a function frame.
// Inputs: frame is the frame to destroy.
// Outputs: Frees label and local binding storage.
// Invariants/Assumptions: Frame memory is heap-allocated.
static void tac_frame_destroy(struct TacFrame* frame) {
  tac_bindings_destroy(&frame->locals);
  free(frame->labels);
  frame->labels = NULL;
  frame->label_count = 0;
  frame->label_capacity = 0;
}

// Purpose: Execute a TAC function and return its result.
// Inputs: interp is the interpreter state; func is the function top-level node.
// Outputs: Returns the integer result produced by TACRETURN.
// Invariants/Assumptions: Function bodies include a TACRETURN on all paths.
static int tac_execute_function(struct TacInterpreter* interp,
                                const struct TopLevel* func,
                                const int* args,
                                size_t num_args) {
  if (func == NULL || func->type != FUNC) {
    tac_interp_error("attempted to call a non-function top-level entry");
  }
  if (func->num_params != num_args) {
    tac_interp_error("argument count mismatch calling %.*s (expected %zu, got %zu)",
                     (int)func->name->len, func->name->start,
                     func->num_params, num_args);
  }

  struct TacFrame frame;
  tac_frame_init(&frame, interp, func->body);

  for (size_t i = 0; i < num_args; i++) {
    tac_write_var(interp, &frame, func->params[i], args[i]);
  }

  struct TACInstr* pc = func->body;
  while (pc != NULL) {
    switch (pc->type) {
      case TACRETURN: {
        int value = pc->instr.tac_return.dst
                        ? tac_eval_val(interp, &frame, pc->instr.tac_return.dst)
                        : 0;
        tac_frame_destroy(&frame);
        return value;
      }
      case TACUNARY: {
        int src = tac_eval_val(interp, &frame, pc->instr.tac_unary.src);
        int result = tac_apply_unary(pc->instr.tac_unary.op, src);
        tac_assign_val(interp, &frame, pc->instr.tac_unary.dst, result);
        break;
      }
      case TACBINARY: {
        int left = tac_eval_val(interp, &frame, pc->instr.tac_binary.src1);
        int right = tac_eval_val(interp, &frame, pc->instr.tac_binary.src2);
        int result = tac_apply_binary(pc->instr.tac_binary.op, left, right);
        tac_assign_val(interp, &frame, pc->instr.tac_binary.dst, result);
        break;
      }
      case TACCOPY: {
        int value = tac_eval_val(interp, &frame, pc->instr.tac_copy.src);
        tac_assign_val(interp, &frame, pc->instr.tac_copy.dst, value);
        break;
      }
      case TACCMP: {
        frame.cmp_left = tac_eval_val(interp, &frame, pc->instr.tac_cmp.src1);
        frame.cmp_right = tac_eval_val(interp, &frame, pc->instr.tac_cmp.src2);
        frame.cmp_valid = true;
        break;
      }
      case TACCOND_JUMP: {
        if (!frame.cmp_valid) {
          tac_interp_error("conditional jump without prior compare");
        }
        if (tac_condition_true(pc->instr.tac_cond_jump.condition,
                               frame.cmp_left,
                               frame.cmp_right)) {
          pc = tac_find_label(&frame, pc->instr.tac_cond_jump.label);
          continue;
        }
        break;
      }
      case TACJUMP: {
        pc = tac_find_label(&frame, pc->instr.tac_jump.label);
        continue;
      }
      case TACLABEL:
        break;
      case TACCALL: {
        int* call_args = NULL;
        if (pc->instr.tac_call.num_args > 0) {
          call_args = (int*)malloc(sizeof(int) * pc->instr.tac_call.num_args);
          if (call_args == NULL) {
            tac_interp_error("memory allocation failed while preparing call arguments");
          }
          for (size_t i = 0; i < pc->instr.tac_call.num_args; i++) {
            call_args[i] = tac_eval_val(interp, &frame, &pc->instr.tac_call.args[i]);
          }
        }
        const struct TopLevel* callee = NULL;
        for (const struct TopLevel* cur = interp->prog->head; cur != NULL; cur = cur->next) {
          if (cur->type == FUNC && compare_slice_to_slice(cur->name, pc->instr.tac_call.func_name)) {
            callee = cur;
            break;
          }
        }
        if (callee == NULL) {
          tac_interp_error("call to unknown function %.*s",
                           (int)pc->instr.tac_call.func_name->len,
                           pc->instr.tac_call.func_name->start);
        }
        int result = tac_execute_function(interp, callee, call_args, pc->instr.tac_call.num_args);
        free(call_args);
        if (pc->instr.tac_call.dst != NULL) {
          tac_assign_val(interp, &frame, pc->instr.tac_call.dst, result);
        }
        break;
      }
      case TACGET_ADDRESS: {
        struct Val* src = pc->instr.tac_get_address.src;
        if (src == NULL || src->val_type != VARIABLE) {
          tac_interp_error("get-address requires a variable source");
        }
        int addr = tac_address_of(interp, &frame, src->val.var_name);
        tac_assign_val(interp, &frame, pc->instr.tac_get_address.dst, addr);
        break;
      }
      case TACLOAD: {
        int addr = tac_eval_val(interp, &frame, pc->instr.tac_load.src_ptr);
        int value = tac_memory_load(&interp->memory, addr);
        tac_assign_val(interp, &frame, pc->instr.tac_load.dst, value);
        break;
      }
      case TACSTORE: {
        int addr = tac_eval_val(interp, &frame, pc->instr.tac_store.dst_ptr);
        int value = tac_eval_val(interp, &frame, pc->instr.tac_store.src);
        tac_memory_store(&interp->memory, addr, value);
        break;
      }
      case TACCOPY_TO_OFFSET: {
        int base = tac_eval_val(interp, &frame, pc->instr.tac_copy_to_offset.dst);
        int value = tac_eval_val(interp, &frame, pc->instr.tac_copy_to_offset.src);
        int addr = base + pc->instr.tac_copy_to_offset.offset;
        tac_memory_store(&interp->memory, addr, value);
        break;
      }
      default:
        tac_interp_error("unsupported TAC instruction %d in interpreter", (int)pc->type);
    }
    pc = pc->next;
  }

  tac_frame_destroy(&frame);
  tac_interp_error("function %.*s terminated without TACRETURN",
                   (int)func->name->len, func->name->start);
  return 0;
}

// Purpose: Initialize global/static variables from TAC top-level entries.
// Inputs: interp is the interpreter state; prog is the TAC program.
// Outputs: Allocates storage and sets initial values in global memory.
// Invariants/Assumptions: Static init lists are stored in consecutive word slots.
static void tac_init_globals(struct TacInterpreter* interp, const struct TACProg* prog) {
  for (const struct TopLevel* cur = prog->head; cur != NULL; cur = cur->next) {
    if (cur->type != STATIC_VAR) {
      continue;
    }
    size_t slots = cur->num_inits > 0 ? cur->num_inits : kTacInterpSingleSlot;
    int base_addr = tac_memory_alloc_range(&interp->memory, slots);
    tac_bindings_reserve(&interp->globals);
    struct TacBinding* binding = &interp->globals.bindings[interp->globals.count++];
    binding->name = cur->name;
    binding->address = base_addr;

    if (cur->init_values == NULL ||
        cur->init_values->init_type == NO_INIT ||
        cur->init_values->init_type == TENTATIVE) {
      tac_memory_store(&interp->memory, base_addr, 0);
      continue;
    }

    struct InitList* init = cur->init_values->init_list;
    size_t idx = 0;
    while (init != NULL) {
      int addr = base_addr + (int)(idx * kTacInterpWordBytes);
      tac_memory_store(&interp->memory, addr, init->value.value);
      init = init->next;
      idx++;
    }
  }
}

// Purpose: Find the "main" function in a TAC program.
// Inputs: prog is the TAC program; name is the function name to locate.
// Outputs: Returns the matching function node or NULL if missing.
// Invariants/Assumptions: Function names are unique at top level.
static const struct TopLevel* tac_find_function(const struct TACProg* prog, const struct Slice* name) {
  for (const struct TopLevel* cur = prog->head; cur != NULL; cur = cur->next) {
    if (cur->type == FUNC && compare_slice_to_slice(cur->name, name)) {
      return cur;
    }
  }
  return NULL;
}

// Purpose: Interpret a TAC program and return the result of main().
// Inputs: prog is the TAC program to execute.
// Outputs: Returns the integer result of the main function.
// Invariants/Assumptions: main takes no parameters in this interpreter.
int tac_interpret_prog(const struct TACProg* prog) {
  if (prog == NULL) {
    tac_interp_error("cannot interpret a NULL TAC program");
  }

  struct TacInterpreter interp;
  interp.prog = prog;
  tac_memory_init(&interp.memory);
  tac_bindings_init(&interp.globals);

  tac_init_globals(&interp, prog);

  struct Slice main_name = { .start = "main", .len = kTacInterpMainNameLen };
  const struct TopLevel* main_func = tac_find_function(prog, &main_name);
  if (main_func == NULL) {
    tac_interp_error("no main function found in TAC program");
  }
  if (main_func->num_params != 0) {
    tac_interp_error("main function expects %zu parameters; interpreter requires 0",
                     main_func->num_params);
  }

  int result = tac_execute_function(&interp, main_func, NULL, 0);
  tac_bindings_destroy(&interp.globals);
  tac_memory_destroy(&interp.memory);
  return result;
}
