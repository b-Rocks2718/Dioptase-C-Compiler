#include "codegen.h"
#include "asm_gen.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

// Allocate a machine instruction node with predictable defaults.
// Returns a zeroed instruction node owned by the arena.
// arena has been initialized before codegen runs.
static struct MachineInstr* alloc_machine_instr(enum MachineInstrType type) {
  struct MachineInstr* instr = arena_alloc(sizeof(struct MachineInstr));
  instr->type = type;
  instr->next = NULL;
  memset(&instr->instr, 0, sizeof(instr->instr));
  return instr;
}

// Append an instruction node to a single-instruction list builder.
static void append_instr(struct MachineInstr** head,
                         struct MachineInstr** tail,
                         struct MachineInstr* instr) {
  if (*head == NULL) {
    *head = instr;
    *tail = instr;
    return;
  }
  (*tail)->next = instr;
  *tail = instr;
}

// Materialize a data label address into a register, optionally with a byte offset.
static void emit_label_address(struct MachineInstr** head,
                               struct MachineInstr** tail,
                               enum Reg addr_reg,
                               enum Reg pc_reg,
                               struct Slice* label,
                               int offset) {
  struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
  movi->instr.movi.ra = addr_reg;
  movi->instr.movi.label = label;
  append_instr(head, tail, movi);

  struct MachineInstr* br = alloc_machine_instr(MACHINE_BR);
  br->instr.reg2.ra = pc_reg;
  br->instr.reg2.rb = R0;
  append_instr(head, tail, br);

  struct MachineInstr* add_pc = alloc_machine_instr(MACHINE_ADD);
  add_pc->instr.alu.ra = addr_reg;
  add_pc->instr.alu.rb = addr_reg;
  add_pc->instr.alu.rc = pc_reg;
  append_instr(head, tail, add_pc);

  if (offset != 0) {
    struct MachineInstr* add_off = alloc_machine_instr(MACHINE_ADD);
    add_off->instr.alu.ra = addr_reg;
    add_off->instr.alu.rb = addr_reg;
    add_off->instr.alu.rc = R0;
    add_off->instr.alu.imm = offset;
    append_instr(head, tail, add_off);
  }
}

// Find the first source location marker in a function body.
// Returns the loc pointer for the first ASM_BOUNDARY, or NULL if none.
static const char* find_function_entry_loc(const struct AsmInstr* instrs) {
  for (const struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) {
    if (cur->type == ASM_BOUNDARY) {
      return cur->instr.asm_boundary.loc;
    }
  }
  return NULL;
}

// Report a codegen error with context, then exit.
// func_name is the current function (may be NULL), instr_type is the ASM opcode.
// Prints an actionable message to stderr and terminates.
ANALYSIS_NORETURN static void codegen_errorf(const struct Slice* func_name,
                           enum AsmInstrType instr_type,
                           const char* fmt,
                           ...) {
  fprintf(stderr, "Compiler Error: codegen: ");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  if (func_name != NULL) {
    fprintf(stderr, " (asm=%d, func=%.*s)\n", (int)instr_type,
            (int)func_name->len, func_name->start);
  } else {
    fprintf(stderr, " (asm=%d)\n", (int)instr_type);
  }
  exit(1);
}

// Select a scratch register that avoids two disallowed registers.
// avoid_a/avoid_b are registers that must not be selected.
// Returns a scratch register distinct from avoid_a and avoid_b.
// At least one scratch register remains available.
static enum Reg pick_scratch_reg(const struct Slice* func_name,
                                 enum AsmInstrType instr_type,
                                 enum Reg avoid_a,
                                 enum Reg avoid_b) {
  if (kScratchRegA != avoid_a && kScratchRegA != avoid_b) {
    return kScratchRegA;
  }
  if (kScratchRegB != avoid_a && kScratchRegB != avoid_b) {
    return kScratchRegB;
  }
  if (kScratchRegC != avoid_a && kScratchRegC != avoid_b) {
    return kScratchRegC;
  }
  codegen_errorf(func_name, instr_type,
                 "no scratch register available (avoid=%d,%d)", (int)avoid_a, (int)avoid_b);
  return kScratchRegA;
}

// Builtin helper names referenced by codegen-generated call sequences.
static struct Slice kBuiltinSmul = {"smul", 4};
static struct Slice kBuiltinSdiv = {"sdiv", 4};
static struct Slice kBuiltinSmod = {"smod", 4};
static struct Slice kBuiltinUmul = {"umul", 4};
static struct Slice kBuiltinUdiv = {"udiv", 4};
static struct Slice kBuiltinUmod = {"umod", 4};
static struct Slice kBuiltinSLeftShift = {"sleft_shift", 11};
static struct Slice kBuiltinSRightShift = {"sright_shift", 12};
static struct Slice kBuiltinULeftShift = {"uleft_shift", 11};
static struct Slice kBuiltinURightShift = {"uright_shift", 12};
static struct Slice kFunctionEpilogueLabel = {"Function Epilogue", 17};
static struct Slice kFunctionPrologueLabel = {"Function Prologue", 17};
static struct Slice kFunctionBodyLabel = {"Function Body", 13};

// Emit a call sequence for a binary builtin that expects args in R1/R2.
static void append_builtin_call(struct MachineInstr** head,
                                struct MachineInstr** tail,
                                struct Slice* label) {
  struct MachineInstr* mov_a = alloc_machine_instr(MACHINE_MOV);
  mov_a->instr.reg2.ra = R1;
  mov_a->instr.reg2.rb = kScratchRegA;
  append_instr(head, tail, mov_a);

  struct MachineInstr* mov_b = alloc_machine_instr(MACHINE_MOV);
  mov_b->instr.reg2.ra = R2;
  mov_b->instr.reg2.rb = kScratchRegB;
  append_instr(head, tail, mov_b);

  struct MachineInstr* call = alloc_machine_instr(MACHINE_CALL);
  call->instr.target.label = label;
  append_instr(head, tail, call);

  struct MachineInstr* mov_result = alloc_machine_instr(MACHINE_MOV);
  mov_result->instr.reg2.ra = kScratchRegA;
  mov_result->instr.reg2.rb = R1;
  append_instr(head, tail, mov_result);
}

// Constants derived from ABI stack layout and short-branch sequencing (byte offsets).
static const int kCondJumpBranchSkip = 4;
static const int kCondJumpJmpSkip = 12;
static const int kZeroOffset = 0;
static const int kSavedBpOffset = 0;
static const int kSavedRaOffset = 4;
static const int kEpilogueStackBytes = 8;

// Load from a data label (optionally with an offset) into a register.
static void emit_data_load(struct MachineInstr** head,
                           struct MachineInstr** tail,
                           const struct Slice* func_name,
                           enum AsmInstrType instr_type,
                           enum Reg dst_reg,
                           const struct Operand* data) {
  if (data->op.data.offset != 0) {
    enum Reg pc_reg = (dst_reg == kScratchRegA) ? kScratchRegB : kScratchRegA;
    emit_label_address(head, tail, dst_reg, pc_reg, data->op.data.label, data->op.data.offset);
    struct MachineInstr* load;
    switch (data->asm_type->type) {
      case BYTE:
        load = alloc_machine_instr(MACHINE_LBA);
        break;
      case DOUBLE:
        load = alloc_machine_instr(MACHINE_LDA);
        break;
      case WORD:
        load = alloc_machine_instr(MACHINE_LWA);
        break;
      default:
        codegen_errorf(func_name, instr_type,
                       "unsupported asm type %d for data operand load", (int)data->asm_type->type);
        return;
    }
    load->instr.mem.ra = dst_reg;
    load->instr.mem.rb = dst_reg;
    load->instr.mem.imm = 0;
    append_instr(head, tail, load);
  } else {
    struct MachineInstr* load;
    switch (data->asm_type->type) {
      case BYTE:
        load = alloc_machine_instr(MACHINE_LB);
        break;
      case DOUBLE:
        load = alloc_machine_instr(MACHINE_LD);
        break;
      case WORD:
        load = alloc_machine_instr(MACHINE_LW);
        break;
      default:
        codegen_errorf(func_name, instr_type,
                       "unsupported asm type %d for data operand load", (int)data->asm_type->type);
        return;
    }
    load->instr.mem.ra = dst_reg;
    load->instr.mem.rb = R0;
    load->instr.mem.imm = kZeroOffset;
    load->instr.mem.label = data->op.data.label;
    append_instr(head, tail, load);
  }
}

static struct MachineInstr* make_data(struct InitList* init, struct AsmType* type);

// Lower one TAC instruction to machine instructions.
struct MachineProg* instr_to_machine(struct Slice* func_name, struct AsmInstr* instr){
  // Uses R9/R10/R11 as scratch registers to avoid clobbering argument registers.
  struct MachineProg* machine_prog = arena_alloc(sizeof(struct MachineProg));
  machine_prog->head = NULL;
  machine_prog->tail = NULL;

  for (struct AsmInstr* cur = instr; cur != NULL; cur = cur->next) {
    struct MachineInstr* head = NULL;
    struct MachineInstr* tail = NULL;
    bool handled = false;

    if (cur->type == ASM_MOV && cur->instr.asm_mov.dst != NULL && cur->instr.asm_mov.src != NULL) {
      if (cur->instr.asm_mov.dst->type == OPERAND_REG && cur->instr.asm_mov.src->type == OPERAND_REG) {
        // Machine: Mov rDst, rSrc
        struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
        mov->instr.reg2.ra = cur->instr.asm_mov.dst->op.reg.reg;
        mov->instr.reg2.rb = cur->instr.asm_mov.src->op.reg.reg;
        append_instr(&head, &tail, mov);
        handled = true;
      } else if (cur->instr.asm_mov.dst->type == OPERAND_REG && cur->instr.asm_mov.src->type == OPERAND_LIT) {
        // Machine: Movi rDst, imm
        struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
        movi->instr.movi.ra = cur->instr.asm_mov.dst->op.reg.reg;
        movi->instr.movi.imm = cur->instr.asm_mov.src->op.lit.value;
        append_instr(&head, &tail, movi);
        handled = true;
      } else if (cur->instr.asm_mov.dst->type == OPERAND_MEMORY && cur->instr.asm_mov.src->type == OPERAND_REG) {
        // Machine: Swa rSrc, [rBase, off]
        struct MachineInstr* store;
        switch (cur->instr.asm_mov.dst->asm_type->type) {
          case BYTE:
            // byte store
            store = alloc_machine_instr(MACHINE_SBA);
            break;
          case DOUBLE:
            // double store
            store = alloc_machine_instr(MACHINE_SDA);
            break;
          case WORD:
            // word store
            store = alloc_machine_instr(MACHINE_SWA);
            break;
          default:
            codegen_errorf(func_name, cur->type,
                           "unsupported asm type %d for memory operand store", (int)cur->instr.asm_mov.dst->asm_type->type);
            break;
        }
        store->instr.mem.ra = cur->instr.asm_mov.src->op.reg.reg;
        store->instr.mem.rb = cur->instr.asm_mov.dst->op.memory.base;
        store->instr.mem.imm = cur->instr.asm_mov.dst->op.memory.offset;
        append_instr(&head, &tail, store);
        handled = true;
      } else if (cur->instr.asm_mov.dst->type == OPERAND_DATA && cur->instr.asm_mov.src->type == OPERAND_REG) {
        if (cur->instr.asm_mov.dst->op.data.offset != 0) {
          // Store through a computed absolute address for label+offset.
          emit_label_address(&head, &tail, kScratchRegB, kScratchRegA,
                             cur->instr.asm_mov.dst->op.data.label, cur->instr.asm_mov.dst->op.data.offset);
          struct MachineInstr* store;
          switch (cur->instr.asm_mov.dst->asm_type->type) {
            case BYTE:
              store = alloc_machine_instr(MACHINE_SBA);
              break;
            case DOUBLE:
              store = alloc_machine_instr(MACHINE_SDA);
              break;
            case WORD:
              store = alloc_machine_instr(MACHINE_SWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for data operand store", (int)cur->instr.asm_mov.dst->asm_type->type);
              break;
          }
          store->instr.mem.ra = cur->instr.asm_mov.src->op.reg.reg;
          store->instr.mem.rb = kScratchRegB;
          store->instr.mem.imm = 0;
          append_instr(&head, &tail, store);
        } else {
          // Machine: Store rSrc, [label]
          struct MachineInstr* store;
          switch (cur->instr.asm_mov.dst->asm_type->type) {
            case BYTE:
              // byte store
              store = alloc_machine_instr(MACHINE_SB);
              break;
            case DOUBLE:
              // double store
              store = alloc_machine_instr(MACHINE_SD);
              break;
            case WORD:
              // word store
              store = alloc_machine_instr(MACHINE_SW);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for data operand store", (int)cur->instr.asm_mov.dst->asm_type->type);
              break;
          }
          store->instr.mem.ra = cur->instr.asm_mov.src->op.reg.reg;
          store->instr.mem.rb = R0;
          store->instr.mem.imm = kZeroOffset;
          store->instr.mem.label = cur->instr.asm_mov.dst->op.data.label;
          append_instr(&head, &tail, store);
        }
        handled = true;
      } else if (cur->instr.asm_mov.dst->type == OPERAND_MEMORY &&
                 (cur->instr.asm_mov.src->type == OPERAND_DATA ||
                  cur->instr.asm_mov.src->type == OPERAND_LIT ||
                  cur->instr.asm_mov.src->type == OPERAND_MEMORY)) {
        enum Reg value_reg = kScratchRegA;
        if (cur->instr.asm_mov.src->type == OPERAND_MEMORY) {
          value_reg = pick_scratch_reg(func_name, cur->type, cur->instr.asm_mov.dst->op.memory.base, cur->instr.asm_mov.src->op.memory.base);
        } else {
          value_reg = pick_scratch_reg(func_name, cur->type, cur->instr.asm_mov.dst->op.memory.base, R0);
        }
        if (cur->instr.asm_mov.src->type == OPERAND_DATA) {
          if (cur->instr.asm_mov.src->op.data.offset == 0) {
            struct MachineInstr* load;
            switch (cur->instr.asm_mov.src->asm_type->type) {
              case BYTE:
                load = alloc_machine_instr(MACHINE_LB);
                break;
              case DOUBLE:
                load = alloc_machine_instr(MACHINE_LD);
                break;
              case WORD:
                load = alloc_machine_instr(MACHINE_LW);
                break;
              default:
                codegen_errorf(func_name, cur->type,
                               "unsupported asm type %d for data operand load",
                               (int)cur->instr.asm_mov.src->asm_type->type);
                break;
            }
            load->instr.mem.ra = value_reg;
            load->instr.mem.rb = R0;
            load->instr.mem.imm = kZeroOffset;
            load->instr.mem.label = cur->instr.asm_mov.src->op.data.label;
            append_instr(&head, &tail, load);
          } else {
            enum Reg pc_reg = pick_scratch_reg(func_name, cur->type, cur->instr.asm_mov.dst->op.memory.base, value_reg);
            emit_label_address(&head, &tail, value_reg, pc_reg,
                               cur->instr.asm_mov.src->op.data.label, cur->instr.asm_mov.src->op.data.offset);
            struct MachineInstr* load;
            switch (cur->instr.asm_mov.src->asm_type->type) {
              case BYTE:
                load = alloc_machine_instr(MACHINE_LBA);
                break;
              case DOUBLE:
                load = alloc_machine_instr(MACHINE_LDA);
                break;
              case WORD:
                load = alloc_machine_instr(MACHINE_LWA);
                break;
              default:
                codegen_errorf(func_name, cur->type,
                               "unsupported asm type %d for data operand load",
                               (int)cur->instr.asm_mov.src->asm_type->type);
                break;
            }
            load->instr.mem.ra = value_reg;
            load->instr.mem.rb = value_reg;
            load->instr.mem.imm = 0;
            append_instr(&head, &tail, load);
          }
        } else if (cur->instr.asm_mov.src->type == OPERAND_LIT) {
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = value_reg;
          movi->instr.movi.imm = cur->instr.asm_mov.src->op.lit.value;
          append_instr(&head, &tail, movi);
        } else if (cur->instr.asm_mov.src->type == OPERAND_MEMORY) {
          struct MachineInstr* load;
          switch (cur->instr.asm_mov.src->asm_type->type) {
            case BYTE:
              load = alloc_machine_instr(MACHINE_LBA);
              break;
            case DOUBLE:
              load = alloc_machine_instr(MACHINE_LDA);
              break;
            case WORD:
              load = alloc_machine_instr(MACHINE_LWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for memory operand load",
                             (int)cur->instr.asm_mov.src->asm_type->type);
              break;
          }
          load->instr.mem.ra = value_reg;
          load->instr.mem.rb = cur->instr.asm_mov.src->op.memory.base;
          load->instr.mem.imm = cur->instr.asm_mov.src->op.memory.offset;
          append_instr(&head, &tail, load);
        }

        struct MachineInstr* store;
        switch (cur->instr.asm_mov.dst->asm_type->type) {
          case BYTE:
            store = alloc_machine_instr(MACHINE_SBA);
            break;
          case DOUBLE:
            store = alloc_machine_instr(MACHINE_SDA);
            break;
          case WORD:
            store = alloc_machine_instr(MACHINE_SWA);
            break;
          default:
            codegen_errorf(func_name, cur->type,
                           "unsupported asm type %d for memory operand store", (int)cur->instr.asm_mov.dst->asm_type->type);
            break;
        }
        store->instr.mem.ra = value_reg;
        store->instr.mem.rb = cur->instr.asm_mov.dst->op.memory.base;
        store->instr.mem.imm = cur->instr.asm_mov.dst->op.memory.offset;
        append_instr(&head, &tail, store);
        handled = true;
      } 
      else if (cur->instr.asm_mov.dst->type == OPERAND_REG && cur->instr.asm_mov.src->type == OPERAND_MEMORY) {
         // Machine: Lwa rDst, [rBase, off]
         struct MachineInstr* load;
         switch (cur->instr.asm_mov.src->asm_type->type) {
           case BYTE:
             // byte load
             load = alloc_machine_instr(MACHINE_LBA);
             break;
           case DOUBLE:
             // double load
             load = alloc_machine_instr(MACHINE_LDA);
             break;
           case WORD:
             // word load
             load = alloc_machine_instr(MACHINE_LWA);
             break;
           default:
             codegen_errorf(func_name, cur->type,
                            "unsupported asm type %d for memory operand load",
                            (int)cur->instr.asm_mov.src->asm_type->type);
             break;
         }
         load->instr.mem.ra = cur->instr.asm_mov.dst->op.reg.reg;
         load->instr.mem.rb = cur->instr.asm_mov.src->op.memory.base;
         load->instr.mem.imm = cur->instr.asm_mov.src->op.memory.offset;
         append_instr(&head, &tail, load);
         handled = true;
       } 
      else if (cur->instr.asm_mov.dst->type == OPERAND_REG && cur->instr.asm_mov.src->type == OPERAND_DATA) {
        emit_data_load(&head, &tail, func_name, cur->type, cur->instr.asm_mov.dst->op.reg.reg, cur->instr.asm_mov.src);
        handled = true;
      }
      
      if (!handled && cur->type == ASM_PUSH && cur->instr.asm_push.src != NULL) {
        if (cur->instr.asm_push.src->type == OPERAND_REG) {
          // Machine: Push rSrc
          struct MachineInstr* push = alloc_machine_instr(MACHINE_PUSH);
          push->instr.reg.ra = cur->instr.asm_push.src->op.reg.reg;
          append_instr(&head, &tail, push);
          handled = true;
        }
      }
    }

    if (!handled && cur->type == ASM_GET_ADDRESS && cur->instr.asm_get_address.dst != NULL && cur->instr.asm_get_address.src != NULL) {
      if (cur->instr.asm_get_address.dst->type == OPERAND_MEMORY && cur->instr.asm_get_address.src->type == OPERAND_MEMORY) {
        // Machine: Add rTmp, rBase, off; Swa rTmp, [rDstBase, dstOff]
        struct MachineInstr* add = alloc_machine_instr(MACHINE_ADD);
        add->instr.alu.ra = kScratchRegB;
        add->instr.alu.rb = cur->instr.asm_get_address.src->op.memory.base;
        add->instr.alu.imm = cur->instr.asm_get_address.src->op.memory.offset;
        append_instr(&head, &tail, add);
        struct MachineInstr* sw = alloc_machine_instr(MACHINE_SWA);
        sw->instr.mem.ra = kScratchRegB;
        sw->instr.mem.rb = cur->instr.asm_get_address.dst->op.memory.base;
        sw->instr.mem.imm = cur->instr.asm_get_address.dst->op.memory.offset;
        append_instr(&head, &tail, sw);
        handled = true;
      } else if (cur->instr.asm_get_address.dst->type == OPERAND_MEMORY && cur->instr.asm_get_address.src->type == OPERAND_DATA) {
        emit_label_address(&head, &tail, kScratchRegB, kScratchRegA,
                           cur->instr.asm_get_address.src->op.data.label, cur->instr.asm_get_address.src->op.data.offset);
        struct MachineInstr* sw = alloc_machine_instr(MACHINE_SWA);
        sw->instr.mem.ra = kScratchRegB;
        sw->instr.mem.rb = cur->instr.asm_get_address.dst->op.memory.base;
        sw->instr.mem.imm = cur->instr.asm_get_address.dst->op.memory.offset;
        append_instr(&head, &tail, sw);
        handled = true;
      } else if (cur->instr.asm_get_address.dst->type == OPERAND_REG && cur->instr.asm_get_address.src->type == OPERAND_MEMORY) {
        struct MachineInstr* add = alloc_machine_instr(MACHINE_ADD);
        add->instr.alu.ra = cur->instr.asm_get_address.dst->op.reg.reg;
        add->instr.alu.rb = cur->instr.asm_get_address.src->op.memory.base;
        add->instr.alu.imm = cur->instr.asm_get_address.src->op.memory.offset;
        append_instr(&head, &tail, add);
        handled = true;
      } else if (cur->instr.asm_get_address.dst->type == OPERAND_REG && cur->instr.asm_get_address.src->type == OPERAND_DATA) {
        enum Reg pc_reg = (cur->instr.asm_get_address.dst->op.reg.reg == kScratchRegA) ? kScratchRegB : kScratchRegA;
        emit_label_address(&head, &tail, cur->instr.asm_get_address.dst->op.reg.reg, pc_reg,
                           cur->instr.asm_get_address.src->op.data.label, cur->instr.asm_get_address.src->op.data.offset);
        handled = true;
      }
    }

    if (!handled) {
      // Generic lowering: load sources into scratch regs, emit op, then store scratch A.
      size_t src_count = 0;
      struct Operand** srcs = get_srcs(cur, &src_count);
      if (src_count == 2) {
        struct Operand* a = srcs[0];
        struct Operand* b = srcs[1];
        if (a->type == OPERAND_REG) {
          // Machine: Mov rScratchA, RA
          struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
          mov->instr.reg2.ra = kScratchRegA;
          mov->instr.reg2.rb = a->op.reg.reg;
          append_instr(&head, &tail, mov);
        } else if (a->type == OPERAND_MEMORY) {
          // Machine: Lwa rScratchA, [rBase, off]
          struct MachineInstr* load;
          switch (a->asm_type->type) {
            case BYTE:
              load = alloc_machine_instr(MACHINE_LBA);
              break;
            case DOUBLE:
              load = alloc_machine_instr(MACHINE_LDA);
              break;
            case WORD:
              load = alloc_machine_instr(MACHINE_LWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for source operand", (int)a->asm_type->type);
              break;
          }
          load->instr.mem.ra = kScratchRegA;
          load->instr.mem.rb = a->op.memory.base;
          load->instr.mem.imm = a->op.memory.offset;
          append_instr(&head, &tail, load);
        } else if (a->type == OPERAND_LIT) {
          // Machine: Movi rScratchA, imm
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = kScratchRegA;
          movi->instr.movi.imm = a->op.lit.value;
          append_instr(&head, &tail, movi);
        } else if (a->type == OPERAND_DATA) {
          emit_data_load(&head, &tail, func_name, cur->type, kScratchRegA, a);
        } else {
          codegen_errorf(func_name, cur->type,
                         "invalid first source operand type %d; expected Reg, Memory, Lit, or Data",
                         (int)a->type);
        }

        if (b->type == OPERAND_REG) {
          // Machine: Mov rScratchB, RB
          struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
          mov->instr.reg2.ra = kScratchRegB;
          mov->instr.reg2.rb = b->op.reg.reg;
          append_instr(&head, &tail, mov);
        } else if (b->type == OPERAND_MEMORY) {
          // Machine: Load rScratchB, [rBase, off]
          struct MachineInstr* load;
          switch (b->asm_type->type) {
            case BYTE:
              load = alloc_machine_instr(MACHINE_LBA);
              break;
            case DOUBLE:
              load = alloc_machine_instr(MACHINE_LDA);
              break;
            case WORD:
              load = alloc_machine_instr(MACHINE_LWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for source operand", (int)b->asm_type->type);
              break;
          }
          load->instr.mem.ra = kScratchRegB;
          load->instr.mem.rb = b->op.memory.base;
          load->instr.mem.imm = b->op.memory.offset;
          append_instr(&head, &tail, load);
        } else if (b->type == OPERAND_LIT) {
          // Machine: Movi rScratchB, imm
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = kScratchRegB;
          movi->instr.movi.imm = b->op.lit.value;
          append_instr(&head, &tail, movi);
        } else if (b->type == OPERAND_DATA) {
          emit_data_load(&head, &tail, func_name, cur->type, kScratchRegB, b);
        } else {
          codegen_errorf(func_name, cur->type,
                         "invalid second source operand type %d; expected Reg, Memory, Lit, or Data",
                         (int)b->type);
        }
      } else if (src_count == 1) {
        struct Operand* a = srcs[0];
        if (a->type == OPERAND_REG) {
          // Machine: Mov rScratchA, RA
          struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
          mov->instr.reg2.ra = kScratchRegA;
          mov->instr.reg2.rb = a->op.reg.reg;
          append_instr(&head, &tail, mov);
        } else if (a->type == OPERAND_MEMORY) {
          // Machine: Lwa rScratchA, [rBase, off]
          struct MachineInstr* load;
          switch (a->asm_type->type) {
            case BYTE:
              load = alloc_machine_instr(MACHINE_LBA);
              break;
            case DOUBLE:
              load = alloc_machine_instr(MACHINE_LDA);
              break;
            case WORD:
              load = alloc_machine_instr(MACHINE_LWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for source operand", (int)a->asm_type->type);
              break;
          }
          load->instr.mem.ra = kScratchRegA;
          load->instr.mem.rb = a->op.memory.base;
          load->instr.mem.imm = a->op.memory.offset;
          append_instr(&head, &tail, load);
        } else if (a->type == OPERAND_LIT) {
          // Machine: Movi rScratchA, imm
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = kScratchRegA;
          movi->instr.movi.imm = a->op.lit.value;
          append_instr(&head, &tail, movi);
        } else if (a->type == OPERAND_DATA) {
          emit_data_load(&head, &tail, func_name, cur->type, kScratchRegA, a);
        } else {
          codegen_errorf(func_name, cur->type,
                         "invalid source operand type %d; expected Reg, Memory, Lit, or Data",
                         (int)a->type);
        }
      }

      switch (cur->type) {
        case ASM_MOV:
        case ASM_VOLATILE_READ:
        case ASM_VOLATILE_WRITE:
          // Same machine move as Mov. The opcode stays distinct until this point.
          break;
        case ASM_CMP: {
          // Machine: Cmp rScratchA, rScratchB
          struct MachineInstr* cmp = alloc_machine_instr(MACHINE_CMP);
          cmp->instr.reg2.ra = kScratchRegA;
          cmp->instr.reg2.rb = kScratchRegB;
          append_instr(&head, &tail, cmp);
          break;
        }
        case ASM_UNARY:
          if (cur->instr.asm_unary.op == COMPLEMENT) {
            // Machine: Not rScratchA, rScratchA
            struct MachineInstr* not_instr = alloc_machine_instr(MACHINE_NOT);
            not_instr->instr.reg2.ra = kScratchRegA;
            not_instr->instr.reg2.rb = kScratchRegA;
            append_instr(&head, &tail, not_instr);
          } else if (cur->instr.asm_unary.op == NEGATE) {
            // Machine: Sub rScratchA, R0, rScratchA
            struct MachineInstr* sub = alloc_machine_instr(MACHINE_SUB);
            sub->instr.alu.ra = kScratchRegA;
            sub->instr.alu.rb = R0;
            sub->instr.alu.rc = kScratchRegA;
            append_instr(&head, &tail, sub);
          } else if (cur->instr.asm_unary.op == UNARY_PLUS) {
            // no-op
          } else {
            codegen_errorf(func_name, cur->type,
                           "unsupported unary op %d; expected COMPLEMENT, NEGATE, or UNARY_PLUS",
                           (int)cur->instr.asm_unary.op);
          }
          break;
        case ASM_BINARY:
          switch (cur->instr.asm_binary.alu_op) {
            case ALU_ADD: {
              // Machine: Add rScratchA, rScratchA, rScratchB
              struct MachineInstr* add = alloc_machine_instr(MACHINE_ADD);
              add->instr.alu.ra = kScratchRegA;
              add->instr.alu.rb = kScratchRegA;
              add->instr.alu.rc = kScratchRegB;
              append_instr(&head, &tail, add);
              break;
            }
            case ALU_SUB: {
              // Machine: Sub rScratchA, rScratchA, rScratchB
              struct MachineInstr* sub = alloc_machine_instr(MACHINE_SUB);
              sub->instr.alu.ra = kScratchRegA;
              sub->instr.alu.rb = kScratchRegA;
              sub->instr.alu.rc = kScratchRegB;
              append_instr(&head, &tail, sub);
              break;
            }
            case ALU_AND: {
              // Machine: And rScratchA, rScratchA, rScratchB
              struct MachineInstr* and_instr = alloc_machine_instr(MACHINE_AND);
              and_instr->instr.alu.ra = kScratchRegA;
              and_instr->instr.alu.rb = kScratchRegA;
              and_instr->instr.alu.rc = kScratchRegB;
              append_instr(&head, &tail, and_instr);
              break;
            }
            case ALU_OR: {
              // Machine: Or rScratchA, rScratchA, rScratchB
              struct MachineInstr* or_instr = alloc_machine_instr(MACHINE_OR);
              or_instr->instr.alu.ra = kScratchRegA;
              or_instr->instr.alu.rb = kScratchRegA;
              or_instr->instr.alu.rc = kScratchRegB;
              append_instr(&head, &tail, or_instr);
              break;
            }
            case ALU_XOR: {
              // Machine: Xor rScratchA, rScratchA, rScratchB
              struct MachineInstr* xor_instr = alloc_machine_instr(MACHINE_XOR);
              xor_instr->instr.alu.ra = kScratchRegA;
              xor_instr->instr.alu.rb = kScratchRegA;
              xor_instr->instr.alu.rc = kScratchRegB;
              append_instr(&head, &tail, xor_instr);
              break;
            }
            case ALU_SMUL: {
              // Machine: Move args into R1/R2, call smul, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinSmul);
              break;
            }
            case ALU_SDIV: {
              // Machine: Move args into R1/R2, call sdiv, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinSdiv);
              break;
            }
            case ALU_SMOD: {
              // Machine: Move args into R1/R2, call smod, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinSmod);
              break;
            }
            case ALU_UMUL: {
              // Machine: Move args into R1/R2, call umul, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinUmul);
              break;
            }
            case ALU_UDIV: {
              // Machine: Move args into R1/R2, call udiv, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinUdiv);
              break;
            }
            case ALU_UMOD: {
              // Machine: Move args into R1/R2, call umod, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinUmod);
              break;
            }
            case ALU_LSL: {
              // Machine: Move args into R1/R2, call uleft_shift, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinULeftShift);
              break;
            }
            case ALU_LSR: {
              // Machine: Move args into R1/R2, call uright_shift, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinURightShift);
              break;
            }
            case ALU_ASL: {
              // Machine: Move args into R1/R2, call sleft_shift, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinSLeftShift);
              break;
            }
            case ALU_ASR: {
              // Machine: Move args into R1/R2, call sright_shift, move result into scratch A.
              append_builtin_call(&head, &tail, &kBuiltinSRightShift);
              break;
            }
            case ALU_MOV: {
              // Mov rB to rA
              struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
              mov->instr.reg2.ra = kScratchRegA;
              mov->instr.reg2.rb = kScratchRegB;
              append_instr(&head, &tail, mov);
              break;
            }
            default:
              codegen_errorf(func_name, cur->type,
                             "unknown ALU op %d; expected a defined ALU_* variant",
                             (int)cur->instr.asm_binary.alu_op);
          }
          break;
        case ASM_JUMP: {
          // Machine: Movi rScratchB, label; Br r0, rScratchB
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = kScratchRegB;
          movi->instr.movi.label = cur->instr.asm_jump.label;
          append_instr(&head, &tail, movi);
          struct MachineInstr* br = alloc_machine_instr(MACHINE_BR);
          br->instr.reg2.ra = R0;
          br->instr.reg2.rb = kScratchRegB;
          append_instr(&head, &tail, br);
          break;
        }
        case ASM_COND_JUMP: {
          // Expand conditional jump into a short branch-over sequence plus relative jump.
          enum MachineInstrType branch_type = MACHINE_BR;
          switch (cur->instr.asm_cond_jump.cond) {
            case CondE:
              branch_type = MACHINE_BZ;
              break;
            case CondNE:
              branch_type = MACHINE_BNZ;
              break;
            case CondG:
              branch_type = MACHINE_BG;
              break;
            case CondGE:
              branch_type = MACHINE_BGE;
              break;
            case CondL:
              branch_type = MACHINE_BL;
              break;
            case CondLE:
              branch_type = MACHINE_BLE;
              break;
            case CondA:
              branch_type = MACHINE_BA;
              break;
            case CondAE:
              branch_type = MACHINE_BAE;
              break;
            case CondB:
              branch_type = MACHINE_BB;
              break;
            case CondBE:
              branch_type = MACHINE_BBE;
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unknown condition %d; expected TAC CondE..CondBE",
                             (int)cur->instr.asm_cond_jump.cond);
          }
          // Machine: B<cond> +4
          struct MachineInstr* cond = alloc_machine_instr(branch_type);
          cond->instr.target.imm = kCondJumpBranchSkip;
          append_instr(&head, &tail, cond);
          // Machine: Jmp +12
          struct MachineInstr* jmp = alloc_machine_instr(MACHINE_JMP);
          jmp->instr.target.imm = kCondJumpJmpSkip;
          append_instr(&head, &tail, jmp);
          // Machine: Movi rScratchB, label; Br r0, rScratchB
          struct MachineInstr* movi = alloc_machine_instr(MACHINE_MOVI);
          movi->instr.movi.ra = kScratchRegB;
          movi->instr.movi.label = cur->instr.asm_cond_jump.label;
          append_instr(&head, &tail, movi);
          struct MachineInstr* br = alloc_machine_instr(MACHINE_BR);
          br->instr.reg2.ra = R0;
          br->instr.reg2.rb = kScratchRegB;
          append_instr(&head, &tail, br);
          break;
        }
        case ASM_LABEL: {
          // Machine: Label
          struct MachineInstr* label = alloc_machine_instr(MACHINE_LABEL);
          label->instr.label.name = cur->instr.asm_label.label;
          append_instr(&head, &tail, label);
          break;
        }
        case ASM_CALL: {
          // Machine: Call label
          struct MachineInstr* call = alloc_machine_instr(MACHINE_CALL);
          call->instr.target.label = cur->instr.asm_call.label;
          append_instr(&head, &tail, call);
          break;
        }
        case ASM_INDIRECT_CALL: {
          
          struct MachineInstr* call = alloc_machine_instr(MACHINE_BRA);
          call->instr.reg2.ra = RA;
          call->instr.reg2.rb = kScratchRegA;
          append_instr(&head, &tail, call);
          break;
        }
        case ASM_PUSH: {
          // Machine: Push rScratchA
          struct MachineInstr* push;
          switch (cur->instr.asm_push.src->asm_type->type) {
            case BYTE:
              push = alloc_machine_instr(MACHINE_PUSHB);
              break;
            case DOUBLE:
              push = alloc_machine_instr(MACHINE_PUSHD);
              break;
            case WORD:
              push = alloc_machine_instr(MACHINE_PUSH);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for push operand",
                             (int)cur->instr.asm_push.src->asm_type->type);
          }
          push->instr.reg.ra = kScratchRegA;
          append_instr(&head, &tail, push);
          break;
        }
        case ASM_RET: {
          // Machine: Comment "Function Epilogue"
          struct MachineInstr* comment = alloc_machine_instr(MACHINE_COMMENT);
          comment->instr.comment.text = &kFunctionEpilogueLabel;
          append_instr(&head, &tail, comment);

          // Machine: Mov sp, bp; Lwa ra, [bp, 4]; Lwa bp, [bp]; Add sp, sp, 8; ret
          struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
          mov->instr.reg2.ra = SP;
          mov->instr.reg2.rb = BP;
          append_instr(&head, &tail, mov);
          struct MachineInstr* lw_ra = alloc_machine_instr(MACHINE_LWA);
          lw_ra->instr.mem.ra = RA;
          lw_ra->instr.mem.rb = BP;
          lw_ra->instr.mem.imm = kSavedRaOffset;
          append_instr(&head, &tail, lw_ra);
          struct MachineInstr* lw_bp = alloc_machine_instr(MACHINE_LWA);
          lw_bp->instr.mem.ra = BP;
          lw_bp->instr.mem.rb = BP;
          lw_bp->instr.mem.imm = kSavedBpOffset;
          append_instr(&head, &tail, lw_bp);
          struct MachineInstr* addi = alloc_machine_instr(MACHINE_ADD);
          addi->instr.alu.ra = SP;
          addi->instr.alu.rb = SP;
          addi->instr.alu.imm = kEpilogueStackBytes;
          append_instr(&head, &tail, addi);
          struct MachineInstr* ret = alloc_machine_instr(MACHINE_RET);
          append_instr(&head, &tail, ret);
          break;
        }
        case ASM_BOUNDARY: {
          if (cur->instr.asm_boundary.loc == NULL) {
            break;
          }
          struct MachineInstr* marker = alloc_machine_instr(MACHINE_DEBUG_LOC);
          marker->instr.debug_loc.loc = cur->instr.asm_boundary.loc;
          append_instr(&head, &tail, marker);
          break;
        }
        case ASM_TRUNC: {
          if (cur->instr.asm_trunc.size == 1) {
            // Machine: tncb rScratchA, rScratchA
            struct MachineInstr* trunc_instr = alloc_machine_instr(MACHINE_TNCB);
            trunc_instr->instr.reg2.ra = kScratchRegA;
            trunc_instr->instr.reg2.rb = kScratchRegA;
            append_instr(&head, &tail, trunc_instr);
          } else if (cur->instr.asm_trunc.size == 2) {
            // Machine: tncd rScratchA, rScratchA
            struct MachineInstr* trunc_instr = alloc_machine_instr(MACHINE_TNCD);
            trunc_instr->instr.reg2.ra = kScratchRegA;
            trunc_instr->instr.reg2.rb = kScratchRegA;
            append_instr(&head, &tail, trunc_instr);
          } else {
            codegen_errorf(func_name, cur->type,
                           "unsupported truncation size %d; expected 1 or 2",
                           (int)cur->instr.asm_trunc.size);
          }
          break;
        }
        case ASM_EXTEND: {
          if (cur->instr.asm_extend.size == 1) {
            // Machine: tncb rScratchA, rScratchA
            struct MachineInstr* ext_instr = alloc_machine_instr(MACHINE_SXTB);
            ext_instr->instr.reg2.ra = kScratchRegA;
            ext_instr->instr.reg2.rb = kScratchRegA;
            append_instr(&head, &tail, ext_instr);
          } else if (cur->instr.asm_extend.size == 2) {
            // Machine: tncd rScratchA, rScratchA
            struct MachineInstr* ext_instr = alloc_machine_instr(MACHINE_SXTD);
            ext_instr->instr.reg2.ra = kScratchRegA;
            ext_instr->instr.reg2.rb = kScratchRegA;
            append_instr(&head, &tail, ext_instr);
          } else {
            codegen_errorf(func_name, cur->type,
                           "unsupported extend size %d; expected 1 or 2",
                           (int)cur->instr.asm_extend.size);
          }
          break;
        }
        case ASM_LOAD:
        case ASM_VOLATILE_LOAD: {
          // Pointer operand is loaded into rScratchA; copy to rScratchB for the base register.
          struct Operand* load_dst = cur->type == ASM_VOLATILE_LOAD
              ? cur->instr.asm_volatile_load.dst
              : cur->instr.asm_load.dst;
          struct MachineInstr* mov_ptr = alloc_machine_instr(MACHINE_MOV);
          mov_ptr->instr.reg2.ra = kScratchRegB;
          mov_ptr->instr.reg2.rb = kScratchRegA;
          append_instr(&head, &tail, mov_ptr);

          struct MachineInstr* load;
          switch (load_dst->asm_type->type) {
            case BYTE:
              load = alloc_machine_instr(MACHINE_LBA);
              break;
            case DOUBLE:
              load = alloc_machine_instr(MACHINE_LDA);
              break;
            case WORD:
              load = alloc_machine_instr(MACHINE_LWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for load destination",
                             (int)load_dst->asm_type->type);
          }
          load->instr.mem.ra = kScratchRegA;
          load->instr.mem.rb = kScratchRegB;
          load->instr.mem.imm = 0;
          append_instr(&head, &tail, load);
          break;
        }
        case ASM_STORE:
        case ASM_VOLATILE_STORE: {
          struct Operand* store_src = cur->type == ASM_VOLATILE_STORE
              ? cur->instr.asm_volatile_store.src
              : cur->instr.asm_store.src;
          struct Operand* store_dst = cur->type == ASM_VOLATILE_STORE
              ? cur->instr.asm_volatile_store.dst
              : cur->instr.asm_store.dst;
          struct MachineInstr* store;
          switch (store_src->asm_type->type) {
            case BYTE:
              store = alloc_machine_instr(MACHINE_SBA);
              break;
            case DOUBLE:
              store = alloc_machine_instr(MACHINE_SDA);
              break;
            case WORD:
              store = alloc_machine_instr(MACHINE_SWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for store destination",
                             (int)store_dst->asm_type->type);
          }
          store->instr.mem.ra = kScratchRegA;
          store->instr.mem.rb = kScratchRegB;
          store->instr.mem.imm = 0;
          append_instr(&head, &tail, store);
          break;
        }
        case ASM_GET_ADDRESS:
          codegen_errorf(func_name, cur->type,
                         "unsupported GetAddress operands; expected dst=Memory and src=Memory or Data");
        default:
          codegen_errorf(func_name, cur->type,
                         "unknown ASM instruction type %d", (int)cur->type);
      }

      struct Operand* dst = get_dst(cur);
      if (dst != NULL) {
        if (dst->type == OPERAND_REG) {
          // Machine: Mov dst, rScratchA
          struct MachineInstr* mov = alloc_machine_instr(MACHINE_MOV);
          mov->instr.reg2.ra = dst->op.reg.reg;
          mov->instr.reg2.rb = kScratchRegA;
          append_instr(&head, &tail, mov);
        } else if (dst->type == OPERAND_MEMORY) {
          // Machine: Swa rScratchA, [rBase, off]
          struct MachineInstr* store;
          switch (dst->asm_type->type) {
            case BYTE:
              store = alloc_machine_instr(MACHINE_SBA);
              break;
            case DOUBLE:
              store = alloc_machine_instr(MACHINE_SDA);
              break;
            case WORD:
              store = alloc_machine_instr(MACHINE_SWA);
              break;
            default:
              codegen_errorf(func_name, cur->type,
                             "unsupported asm type %d for destination operand", (int)dst->asm_type->type);
              break;
          }
          store->instr.mem.ra = kScratchRegA;
          store->instr.mem.rb = dst->op.memory.base;
          store->instr.mem.imm = dst->op.memory.offset;
          append_instr(&head, &tail, store);
        } else if (dst->type == OPERAND_DATA) {
          if (dst->op.data.offset != 0) {
            // Preserve scratch A while computing address for label+offset.
            struct MachineInstr* push = alloc_machine_instr(MACHINE_PUSH);
            push->instr.reg.ra = kScratchRegA;
            append_instr(&head, &tail, push);

            emit_label_address(&head, &tail, kScratchRegB, kScratchRegA,
                               dst->op.data.label, dst->op.data.offset);

            struct MachineInstr* pop = alloc_machine_instr(MACHINE_POP);
            pop->instr.reg.ra = kScratchRegA;
            append_instr(&head, &tail, pop);

            struct MachineInstr* store;
            switch (dst->asm_type->type) {
              case BYTE:
                store = alloc_machine_instr(MACHINE_SBA);
                break;
              case DOUBLE:
                store = alloc_machine_instr(MACHINE_SDA);
                break;
              case WORD:
                store = alloc_machine_instr(MACHINE_SWA);
                break;
              default:
                codegen_errorf(func_name, cur->type,
                               "unsupported asm type %d for destination operand", (int)dst->asm_type->type);
                break;
            }
            store->instr.mem.ra = kScratchRegA;
            store->instr.mem.rb = kScratchRegB;
            store->instr.mem.imm = 0;
            append_instr(&head, &tail, store);
          } else {
            // Machine: Sw rScratchA, [label]
            struct MachineInstr* store;
            switch (dst->asm_type->type) {
              case BYTE:
                store = alloc_machine_instr(MACHINE_SB);
                break;
              case DOUBLE:
                store = alloc_machine_instr(MACHINE_SD);
                break;
              case WORD:
                store = alloc_machine_instr(MACHINE_SW);
                break;
              default:
                codegen_errorf(func_name, cur->type,
                               "unsupported asm type %d for destination operand", (int)dst->asm_type->type);
                break;
            }
            store->instr.mem.ra = kScratchRegA;
            store->instr.mem.rb = R0;
            store->instr.mem.imm = kZeroOffset;
            store->instr.mem.label = dst->op.data.label;
            append_instr(&head, &tail, store);
          }
        } else {
          codegen_errorf(func_name, cur->type,
                         "invalid destination operand type %d; expected Reg, Memory, or Data",
                         (int)dst->type);
        }
      }
    }

    if (head != NULL) {
      if (machine_prog->head == NULL) {
        machine_prog->head = head;
        machine_prog->tail = tail;
      } else {
        machine_prog->tail->next = head;
        machine_prog->tail = tail;
      }
    }
  }

  return machine_prog;
}

// Lower one top-level TAC object to machine code.
struct MachineProg* top_level_to_machine(struct AsmTopLevel* asm_top){
  struct MachineProg* machine_prog = arena_alloc(sizeof(struct MachineProg));
  machine_prog->head = NULL;
  machine_prog->tail = NULL;

  if (asm_top->type == ASM_FUNC){
    //  .global func         # optional global label
    //  func:
    //    # function prologue
    //    push ra            # save return address
    //    push bp            # save base pointer
    //    mov  bp, sp        # set base pointer to current stack pointer
    //    # function body

    struct MachineInstr* newline = arena_alloc(sizeof(struct MachineInstr));
    newline->type = MACHINE_NEWLINE;

    struct MachineInstr* label = arena_alloc(sizeof(struct MachineInstr));
    if (asm_top->top.asm_func.global) {
      // Machine: Label <func>
      struct MachineInstr* global = arena_alloc(sizeof(struct MachineInstr));
      global->type = MACHINE_GLOBAL;
      global->instr.target.label = asm_top->top.asm_func.name;
      global->next = label;
      newline->next = global;
    } else {
      newline->next = label;
    }

    // Machine: Label <func>
    label->type = MACHINE_LABEL;
    label->instr.label.name = asm_top->top.asm_func.name;

    const char* entry_loc = find_function_entry_loc(asm_top->top.asm_func.body);

    // Machine: Comment "Function Prologue"
    struct MachineInstr* prologue_comment = arena_alloc(sizeof(struct MachineInstr));
    prologue_comment->type = MACHINE_COMMENT;
    prologue_comment->instr.comment.text = &kFunctionPrologueLabel;
    if (entry_loc != NULL) {
      // Emit a line marker at function entry so debugger locations are valid at the label.
      struct MachineInstr* entry_marker = alloc_machine_instr(MACHINE_DEBUG_LOC);
      entry_marker->instr.debug_loc.loc = entry_loc;
      label->next = entry_marker;
      entry_marker->next = prologue_comment;
    } else {
      label->next = prologue_comment;
    }
    
    // Machine: Push ra
    struct MachineInstr* push_ra = arena_alloc(sizeof(struct MachineInstr));
    push_ra->type = MACHINE_PUSH;
    push_ra->instr.reg.ra = RA;
    prologue_comment->next = push_ra;

    // Machine: Push bp
    struct MachineInstr* push_bp = arena_alloc(sizeof(struct MachineInstr));
    push_bp->type = MACHINE_PUSH;
    push_bp->instr.reg.ra = BP;
    push_ra->next = push_bp;

    // Machine: Mov bp, sp
    struct MachineInstr* set_bp = arena_alloc(sizeof(struct MachineInstr));
    set_bp->type = MACHINE_MOV;
    set_bp->instr.reg2.ra = BP;
    set_bp->instr.reg2.rb = SP;
    push_bp->next = set_bp;
    set_bp->next = NULL;

    // Emit stack layout comments for user-visible locals (if debug markers exist).
    struct MachineInstr* locals_tail = set_bp;
    if (asm_top->top.asm_func.locals != NULL) {
      for (struct DebugLocal* local = asm_top->top.asm_func.locals; local != NULL; local = local->next) {
        struct MachineInstr* local_instr = alloc_machine_instr(MACHINE_DEBUG_LOCAL);
        local_instr->instr.debug_local.name = local->name;
        local_instr->instr.debug_local.offset = local->offset;
        local_instr->instr.debug_local.size = local->size;
        locals_tail->next = local_instr;
        locals_tail = local_instr;
      }
    }

    // Machine: Comment "Function Body"
    struct MachineInstr* body_comment = arena_alloc(sizeof(struct MachineInstr));
    body_comment->type = MACHINE_COMMENT;
    body_comment->instr.comment.text = &kFunctionBodyLabel;
    locals_tail->next = body_comment;

    // append prologue to machine_prog
    machine_prog->head = newline;
    machine_prog->tail = body_comment;

    // function body
    struct MachineProg* body_instrs = instr_to_machine(asm_top->top.asm_func.name, asm_top->top.asm_func.body);
    machine_prog->tail->next = body_instrs->head;
    machine_prog->tail = body_instrs->tail;

  } else if (asm_top->type == ASM_STATIC_VAR || asm_top->type == ASM_STATIC_CONST){
    struct Slice* name;
    bool global;
    struct InitList* init;
    if (asm_top->type == ASM_STATIC_VAR) {
      name = asm_top->top.asm_static_var.name;
      global = asm_top->top.asm_static_var.global;
      init = asm_top->top.asm_static_var.init_values;
    } else {
      name = asm_top->top.asm_static_const.name;
      global = asm_top->top.asm_static_const.global;
      init = asm_top->top.asm_static_const.init_values;
    }
    struct AsmSymbolEntry* sym_entry = asm_symbol_table_get(asm_symbol_table, name);
    if (sym_entry == NULL) {
      printf("Compiler Error: Undefined symbol '%.*s' in codegen\n", 
        (int)name->len, name->start);
      exit(1);
    }
    struct AsmType* type = sym_entry->type;

    // emit .align directive
    struct MachineInstr* align_instr = arena_alloc(sizeof(struct MachineInstr));
    align_instr->type = MACHINE_ALIGN;
    align_instr->instr.target.imm = asm_type_alignment(type);
    machine_prog->head = align_instr;
    machine_prog->tail = align_instr;

    if (global) {
      // Machine: .global var
      struct MachineInstr* global_dir = arena_alloc(sizeof(struct MachineInstr));
      global_dir->type = MACHINE_GLOBAL;
      global_dir->instr.target.label = name;

      // append global to machine_prog
      machine_prog->tail->next = global_dir;
      machine_prog->tail = global_dir;
    }

    // static variable
    struct MachineInstr* data_label = alloc_machine_instr(MACHINE_LABEL);
    data_label->instr.label.name = name;
    machine_prog->tail->next = data_label;
    machine_prog->tail = data_label;

    // Machine: .space or .fill for static data
    struct MachineInstr* data_instr = make_data(init, type);

    // append data instructions to machine_prog
    struct MachineInstr* data_tail = data_instr;
    while (data_tail->next != NULL) {
      data_tail = data_tail->next;
    }
    machine_prog->tail->next = data_instr;
    machine_prog->tail = data_tail;
  } else if (asm_top->type == ASM_SECTION){
    // directive
    struct MachineInstr* dir_instr = arena_alloc(sizeof(struct MachineInstr));
    dir_instr->type = MACHINE_SECTION;
    dir_instr->instr.target.label = asm_top->top.asm_section.name;

    // append data_instr to machine_prog
    machine_prog->head = dir_instr;
    machine_prog->tail = dir_instr;
  } else if (asm_top->type == ASM_ALIGN) {
    // directive
    struct MachineInstr* align_instr = arena_alloc(sizeof(struct MachineInstr));
    align_instr->type = MACHINE_ALIGN;
    align_instr->instr.target.imm = asm_top->top.asm_align.alignment;

    // append data_instr to machine_prog
    machine_prog->head = align_instr;
    machine_prog->tail = align_instr;
  } else {
    // Error
    printf("Compiler Error: Unknown AsmTopLevelType in codegen\n");
    exit(1);
  }

  return machine_prog;
}

// Convert a static initializer into an assembly data declaration.
static struct MachineInstr* make_data(struct InitList* init, struct AsmType* type){
  if (init == NULL) {
    // Tentative definitions emit zero-filled storage for the full symbol size.
    struct MachineInstr* instr = alloc_machine_instr(MACHINE_SPACE);
    instr->instr.target.imm = (int)asm_type_size(type);
    return instr;
  }

  struct MachineInstr* instr = NULL;
  struct MachineInstr* tail = NULL;
  for (struct InitList* cur = init; cur != NULL; cur = cur->next){
    struct MachineInstr* cur_instr = alloc_machine_instr(MACHINE_FILL);

    bool was_string = false;

    switch (cur->value->int_type) {
      case CHAR_INIT:
      case UCHAR_INIT:
        cur_instr->type = MACHINE_FILB;
        break;
      case SHORT_INIT:
      case USHORT_INIT:
        cur_instr->type = MACHINE_FILD;
        break;
      case INT_INIT:
      case UINT_INIT:
        cur_instr->type = MACHINE_FILL;
        break;
      case POINTER_INIT:
        cur_instr->type = MACHINE_FILL;
        cur_instr->instr.target.label = cur->value->value.pointer;
        break;
      case LONG_INIT:
      case ULONG_INIT:
        {
          uint64_t raw = cur->value->value.num;
          uint32_t low = (uint32_t)(raw & 0xFFFFFFFFu);
          uint32_t high = (uint32_t)((raw >> 32) & 0xFFFFFFFFu);

          struct MachineInstr* low_instr = alloc_machine_instr(MACHINE_FILL);
          low_instr->instr.target.imm = (int32_t)low;

          struct MachineInstr* high_instr = alloc_machine_instr(MACHINE_FILL);
          high_instr->instr.target.imm = (int32_t)high;

          if (instr == NULL) {
            instr = low_instr;
            tail = low_instr;
          } else {
            tail->next = low_instr;
            tail = low_instr;
          }
          tail->next = high_instr;
          tail = high_instr;
          was_string = true;
          break;
        }
      case ZERO_INIT:
        cur_instr->type = MACHINE_SPACE;
        break;
      case STRING_INIT:
        was_string = true;
        // emit sequence of FILB instructions for each character in the string
        // padding/null bytes are emitted explicitly via ZERO_INIT nodes
        for (size_t i = 0; i < cur->value->value.string->len; i++) {
          struct MachineInstr* char_instr = alloc_machine_instr(MACHINE_FILB);
          char_instr->instr.target.imm = (int)(unsigned char)cur->value->value.string->start[i];
          if (instr == NULL){
            instr = char_instr;
            tail = char_instr;
          } else {
            tail->next = char_instr;
            tail = char_instr;
          }
        }

        break;
      default:
        codegen_errorf(NULL, 0,
                       "unsupported static initializer type %d", (int)cur->value->int_type);
    }

    if (!was_string){
      // set value and append to list (string does this on its own)
      if (cur_instr->instr.target.label == NULL) {
        cur_instr->instr.target.imm = (int)cur->value->value.num;
      }

      if (instr == NULL){
        instr = cur_instr;
        tail = cur_instr;
      } else {
        tail->next = cur_instr;
        tail = cur_instr;
      }
    }
  }

  return instr;
}

// Lower the complete TAC program to machine code.
struct MachineProg* prog_to_machine(struct AsmProg* asm_prog){
  struct MachineProg* machine_prog = arena_alloc(sizeof(struct MachineProg));
  machine_prog->head = NULL;
  machine_prog->tail = NULL;

  for (struct AsmTopLevel* top = asm_prog->head; top != NULL; top = top->next){
    struct MachineProg* top_instrs = top_level_to_machine(top);
    if (machine_prog->head == NULL){
      machine_prog->head = top_instrs->head;
      machine_prog->tail = top_instrs->tail;
    } else {
      machine_prog->tail->next = top_instrs->head;
      machine_prog->tail = top_instrs->tail;
    }
  }

  return machine_prog;
}
