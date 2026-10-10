#include "regalloc.h"

#include "arena.h"
#include "checked_alloc.h"
#include "codegen.h"
#include "exit_codes.h"
#include "interference_graph.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Report an inconsistent coloring of func's interference graph.
static void register_map_error(const struct AsmFunc* func, const char* fmt, ...) {
  fprintf(stderr, "Register allocation error in function %.*s: ", (int)func->name->len, func->name->start);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(BCC_EXIT_INTERNAL);
}

// color_graph's contract: every register node gets a distinct color in
// [0, NUM_ALLOCATABLE_REGS) (they all interfere with each other), and each
// pseudo gets one of those colors or -1 when spilled. A pseudo's register is
// the register node that shares its color. The coloring is checked rather than
// trusted, since a mistake here would silently put two live values in one
// register.
struct RegisterMap* build_register_map(struct InterferenceGraph* ig, struct AsmFunc* func) {
  // make a map from color to the corresponding register
  enum Reg color_regs[NUM_ALLOCATABLE_REGS];
  for (size_t c = 0; c < NUM_ALLOCATABLE_REGS; c++) {
    color_regs[c] = R0;
  }
  for (size_t i = 0; i < NUM_ALLOCATABLE_REGS; i++) {
    const struct InterferenceNode* node = &ig->nodes[i];
    int color = node->color;
    if (color < 0 || color >= NUM_ALLOCATABLE_REGS) {
      register_map_error(func, "register node r%d has color %d; every register node needs a color "
                         "in 0..%d", (int)node->id.reg, color, NUM_ALLOCATABLE_REGS - 1);
    }
    if (color_regs[color] != R0) {
      register_map_error(func, "registers r%d and r%d share a color", (int)color_regs[color],
                         (int)node->id.reg);
    }
    color_regs[color] = node->id.reg;
  }

  struct RegisterMap* map = checked_calloc(1, sizeof(*map), "Register allocation", "creating the register map");
  map->pseudo_ids = &ig->pseudo_ids;
  map->num_pseudos = ig->num_nodes - NUM_ALLOCATABLE_REGS;
  map->regs = checked_calloc(map->num_pseudos, sizeof(*map->regs), "Register allocation",
                             "creating the register map");

  // assign psuedos to registers based on their color
  // also track the callee-saved registers used
  func->callee_saved_regs = 0;
  for (size_t id = 0; id < map->num_pseudos; id++) {
    int color = ig->nodes[NUM_ALLOCATABLE_REGS + id].color;
    if (color == -1) {
      // spill this pseudo
      map->regs[id] = R0;
      continue;
    }
    if (color < 0 || color >= NUM_ALLOCATABLE_REGS) {
      const struct Slice* name = ig->nodes[NUM_ALLOCATABLE_REGS + id].id.pseudo;
      register_map_error(func, "pseudo %.*s has color %d; expected -1 (spilled) or 0..%d",
                         (int)name->len, name->start, color, NUM_ALLOCATABLE_REGS - 1);
    }
    // assign the pseudo to the register corresponding to its color
    enum Reg reg = color_regs[color];
    map->regs[id] = reg;
    if (is_callee_saved(reg)) {
      func->callee_saved_regs |= UINT32_C(1) << reg;
    }
  }
  return map;
}

enum Reg register_map_get(const struct RegisterMap* map, const struct Slice* name) {
  uint32_t id = slice_index_get(map->pseudo_ids, name);
  return id == SLICE_INDEX_NONE ? R0 : map->regs[id];
}

void destroy_register_map(struct RegisterMap* map) {
  free(map->regs);
  free(map);
}

// Return a fresh Reg operand for reg with the given type. Operands may be
// shared between instructions, so replacements are new operands assigned to
// operand fields, never edits of the pseudo operand itself.
static struct Operand* make_reg_operand(enum Reg reg, struct AsmType* asm_type) {
  struct Operand* opr = arena_alloc(sizeof(*opr));
  opr->type = OPERAND_REG;
  opr->asm_type = asm_type;
  opr->op.reg.reg = reg;
  return opr;
}

// Return true if instr is a Mov from a register to the same register.
static bool is_self_move(const struct AsmInstr* instr) {
  if (instr->type != ASM_MOV) {
    return false;
  }
  const struct Operand* dst = instr->instr.asm_mov.dst;
  const struct Operand* src = instr->instr.asm_mov.src;
  return dst->type == OPERAND_REG && src->type == OPERAND_REG && dst->op.reg.reg == src->op.reg.reg;
}

// Replace every pseudo operand that coloring placed in a register with that register
void assign_registers(struct AsmFunc* func, struct RegisterMap* reg_map) {
  struct AsmInstr* prev = NULL;
  struct AsmInstr* instr = func->body;
  while (instr != NULL) {
    struct AsmInstr* next = instr->next;

    struct OperandSlots slots = asm_operand_slots(instr);
    for (size_t i = 0; i < slots.count; i++) {
      struct Operand* opr = *slots.slot[i].field;
      if (opr == NULL || opr->type != OPERAND_PSEUDO) {
        continue;
      }
      enum Reg reg = register_map_get(reg_map, opr->op.pseudo.name);
      if (reg != R0) {
        *slots.slot[i].field = make_reg_operand(reg, opr->asm_type);
      }
    }

    if (is_self_move(instr)) {
      // remove `mov rX, rX` instructions
      if (prev == NULL) {
        func->body = next;
      } else {
        prev->next = next;
      }
    } else {
      prev = instr;
    }
    instr = next;
  }
}

// Allocates registers for the body of a function based on the interference graph.
void alloc_body_registers(struct AsmFunc* func) {
  struct InterferenceGraph* ig = build_interference_graph(func);
  color_graph(ig);
  struct RegisterMap* reg_map = build_register_map(ig, func);
  assign_registers(func, reg_map);
  destroy_register_map(reg_map);
  destroy_interference_graph(ig);
}

// Allocate registers for every function in prog. Pseudos left unallocated
// keep their memory home: assign_stack_slots gives them frame slots.
void allocate_registers(struct AsmProg* prog) {
  // iterate over functions and allocate registers for each one
  for (struct AsmTopLevel* top = prog->head; top != NULL; top = top->next) {
    // allocate registers for the function if this top-level item is a function
    if (top->type == ASM_FUNC) {
      alloc_body_registers(&top->top.asm_func);
    }
  }
}
