#include "codegen.h"
#include "exit_codes.h"
#include "asm_gen.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

// Codegen lowers the ASM IR, after pseudo replacement, into Dioptase machine
// instructions. Every operand reaching this pass is Reg, Lit, Memory, or Data.
//
// Most instructions go through one fixed scratch template (lower_via_scratch):
// sources are loaded into kScratchRegA/kScratchRegB, the operation computes
// into kScratchRegA, and kScratchRegA is written back to the destination. Mov
// and GetAddress have direct lowerings that skip the template where the operand
// shapes allow it. The scratch registers r9-r10 are caller-saved and are not
// argument registers (docs/abi.md), so clobbering them never disturbs argument
// setup for a pending call.

// Accumulates machine instructions in emission order, plus the context that
// diagnostics report.
struct Emitter {
  struct MachineInstr* head;
  struct MachineInstr* tail;
  // Function being lowered, or NULL while emitting data and directives.
  const struct AsmFunc* func;
  // ASM instruction being lowered, or NULL outside instruction lowering.
  const struct AsmInstr* cur;
};

// Addressing form of a load or store: [base, imm], or a PC-relative data label.
enum MemForm {
  MEM_BASE_OFFSET,
  MEM_LABEL,
};

// Builtin helper names referenced by codegen-generated call sequences.
static struct Slice kBuiltinSmul = {"smul", 4};
static struct Slice kBuiltinSdiv = {"sdiv", 4};
static struct Slice kBuiltinSmod = {"smod", 4};
static struct Slice kBuiltinUmul = {"umul", 4};
static struct Slice kBuiltinUdiv = {"udiv", 4};
static struct Slice kBuiltinUmod = {"umod", 4};
static struct Slice kFunctionEpilogueLabel = {"Function Epilogue", 17};
static struct Slice kFunctionPrologueLabel = {"Function Prologue", 17};
static struct Slice kFunctionBodyLabel = {"Function Body", 13};

// Immediate field widths from docs/ISA.md. All are sign-extended except the
// shift amount and the bitwise byte.
static const unsigned kAluArithImmBits = 12;   // add/addc/sub/subb, "Arithmetic"
static const unsigned kAbsMemImmBits = 12;     // swa/lwa/..., "Absolute Addressing"
static const unsigned kAbsMemMaxScaleShift = 3; // the zz field: imm scaled by 2^0..2^3
static const unsigned kPcRelMemImmBits = 16;   // sw/lw/... with a base, "PC-Relative Addressing"
static const unsigned kBranchImmBits = 22;     // "Immediate Branches", counted in instructions
static const int kBranchImmScale = 4;          // the assembler takes byte offsets: imm = 4 * i

// Largest amount the 5-bit shift immediate can hold (docs/ISA.md, "Shifts").
static const int kShiftImmMax = 31;

// Bitwise immediates are one byte placed at byte lane 0-3 (docs/ISA.md,
// "Bitwise logic").
static const uint32_t kBitwiseImmByteMask = 0xFF;
static const int kBitwiseImmLanes = 4;

// Low 10 bits that lui cannot set; its 22-bit field supplies bits 10-31.
static const uint32_t kLuiLowMask = 0x3FF;

// Report a codegen error with the current function and ASM opcode, then exit.
ANALYSIS_NORETURN static void codegen_errorf(const struct Emitter* e,
                                             const char* fmt,
                                             ...) {
  fprintf(stderr, "Compiler Error: codegen: ");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  if (e->func != NULL && e->cur != NULL) {
    fprintf(stderr, " (asm=%d, func=%.*s)\n", (int)e->cur->type,
            (int)e->func->name->len, e->func->name->start);
  } else if (e->func != NULL) {
    fprintf(stderr, " (func=%.*s)\n", (int)e->func->name->len, e->func->name->start);
  } else {
    fprintf(stderr, "\n");
  }
  exit(BCC_EXIT_INTERNAL);
}

// ---------------------------------------------------------------------------
// Instruction builders
// ---------------------------------------------------------------------------

// Return true if value fits a sign-extended field of the given width.
static bool fits_signed_bits(int64_t value, unsigned bits) {
  int64_t min = -((int64_t)1 << (bits - 1));
  int64_t max = ((int64_t)1 << (bits - 1)) - 1;
  return value >= min && value <= max;
}

// Return true if `type` can carry imm in its immediate field, following the
// encodings in docs/ISA.md and the assembler macros in
// Dioptase-Assembler/docs/syntax.md. Instructions with no immediate form
// (register-only ops, register branches, extends) and non-instructions
// (directives, labels, comments) return false. Bitwise immediates must also be
// nonnegative, because the printer writes imm as a signed decimal and the
// assembler rejects negative bitwise immediates.
static bool is_encodable_imm(enum MachineInstrType type, int imm) {
  switch (type) {
    // Arithmetic: 12-bit signed. Immediate sub/subb compute `imm - rB`.
    case MACHINE_ADD:
    case MACHINE_ADDC:
    case MACHINE_SUB:
    case MACHINE_SUBB:
    // `cmp rA, imm` is a sub with the arithmetic immediate.
    case MACHINE_CMP:
      return fits_signed_bits(imm, kAluArithImmBits);

    // Bitwise: one byte at any of the four byte lanes.
    case MACHINE_AND:
    case MACHINE_NAND:
    case MACHINE_OR:
    case MACHINE_NOR:
    case MACHINE_XOR:
    case MACHINE_XNOR:
    case MACHINE_NOT:
      if (imm < 0) {
        return false;
      }
      for (int lane = 0; lane < kBitwiseImmLanes; lane++) {
        uint32_t lane_mask = kBitwiseImmByteMask << (8 * lane);
        if (((uint32_t)imm & ~lane_mask) == 0) {
          return true;
        }
      }
      return false;

    // Shifts and rotates: 5-bit unsigned amount.
    case MACHINE_LSL:
    case MACHINE_LSR:
    case MACHINE_ASR:
    case MACHINE_ROTL:
    case MACHINE_ROTR:
    case MACHINE_LSLC:
    case MACHINE_LSRC:
      return imm >= 0 && imm <= kShiftImmMax;

    case MACHINE_LUI:
      return ((uint32_t)imm & kLuiLowMask) == 0;

    // Absolute addressing: a 12-bit signed field scaled by 2^z, z in 0..3. The
    // assembler picks the smallest z that represents imm exactly.
    case MACHINE_SWA:
    case MACHINE_LWA:
    case MACHINE_SDA:
    case MACHINE_LDA:
    case MACHINE_SBA:
    case MACHINE_LBA:
      for (unsigned shift = 0; shift <= kAbsMemMaxScaleShift; shift++) {
        int64_t scale = (int64_t)1 << shift;
        if (imm % scale == 0 && fits_signed_bits(imm / scale, kAbsMemImmBits)) {
          return true;
        }
      }
      return false;

    // PC-relative with a base register: 16-bit signed. (The base-less form has
    // a 21-bit field, but codegen always prints a base when imm is numeric.)
    case MACHINE_SW:
    case MACHINE_LW:
    case MACHINE_SD:
    case MACHINE_LD:
    case MACHINE_SB:
    case MACHINE_LB:
      return fits_signed_bits(imm, kPcRelMemImmBits);

    // Immediate branches and `jmp imm` (an alias for `br imm`): a byte offset
    // that is a multiple of 4 and fits the 22-bit instruction count.
    case MACHINE_BR:
    case MACHINE_BZ:
    case MACHINE_BNZ:
    case MACHINE_BS:
    case MACHINE_BNS:
    case MACHINE_BC:
    case MACHINE_BNC:
    case MACHINE_BO:
    case MACHINE_BNO:
    case MACHINE_BPS:
    case MACHINE_BNPS:
    case MACHINE_BG:
    case MACHINE_BGE:
    case MACHINE_BL:
    case MACHINE_BLE:
    case MACHINE_BA:
    case MACHINE_BAE:
    case MACHINE_BB:
    case MACHINE_BBE:
    case MACHINE_JMP:
      return imm % kBranchImmScale == 0 && fits_signed_bits(imm / kBranchImmScale, kBranchImmBits);

    // Macros that materialize a full 32-bit value (movi: lui + addi; call:
    // movu + movl).
    case MACHINE_MOVI:
    case MACHINE_CALL:
      return true;

    default:
      return false;
  }
}

// Append a zeroed instruction of the given kind; the caller fills its payload.
static struct MachineInstr* emit(struct Emitter* e, enum MachineInstrType type) {
  struct MachineInstr* instr = arena_alloc(sizeof(struct MachineInstr));
  memset(instr, 0, sizeof(*instr));
  instr->type = type;
  if (e->head == NULL) {
    e->head = instr;
  } else {
    e->tail->next = instr;
  }
  e->tail = instr;
  return instr;
}

// Three-register ALU form: `op ra, rb, rc`.
static void emit_alu_rrr(struct Emitter* e, enum MachineInstrType type,
                         enum Reg ra, enum Reg rb, enum Reg rc) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.alu.ra = ra;
  instr->instr.alu.rb = rb;
  instr->instr.alu.rc = rc;
}

// ALU immediate form: `op ra, rb, imm`. An imm of 0 prints as the register
// form with rc = r0, which computes the same result.
static void emit_alu_rri(struct Emitter* e, enum MachineInstrType type,
                         enum Reg ra, enum Reg rb, int imm) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.alu.ra = ra;
  instr->instr.alu.rb = rb;
  instr->instr.alu.imm = imm;
}

// Load upper immediate: `lui ra, imm`.
static void emit_lui(struct Emitter* e, enum Reg ra, int imm) {
  struct MachineInstr* instr = emit(e, MACHINE_LUI);
  instr->instr.movi.ra = ra;
  instr->instr.movi.imm = imm;
}

// Two-register form shared by mov, not, cmp, truncate/sign-extend, and register branches.
static void emit_reg2(struct Emitter* e, enum MachineInstrType type, enum Reg ra, enum Reg rb) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.reg2.ra = ra;
  instr->instr.reg2.rb = rb;
}

// Single-register form used by push and pop.
static void emit_reg1(struct Emitter* e, enum MachineInstrType type, enum Reg ra) {
  emit(e, type)->instr.reg.ra = ra;
}

// Load or store addressing [base, imm].
static void emit_mem_base(struct Emitter* e, enum MachineInstrType type,
                          enum Reg ra, enum Reg base, int imm) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.mem.ra = ra;
  instr->instr.mem.rb = base;
  instr->instr.mem.imm = imm;
}

// PC-relative load or store of label + imm, printed as `[label + imm]`. This
// is the base-less form, whose 21-bit offset reaches +-1 MiB from the
// instruction (docs/ISA.md, "PC-Relative Addressing (immediate)"); the
// assembler resolves label + imm (README, "Address reach limits").
static void emit_mem_label(struct Emitter* e, enum MachineInstrType type,
                           enum Reg ra, struct Slice* label, int imm) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.mem.ra = ra;
  instr->instr.mem.label = label;
  instr->instr.mem.imm = imm;
}

// Load an immediate. Values that fit the 12-bit add immediate use a single
// `add ra, r0, imm`; anything else uses movi, which the assembler always
// expands to two instructions (lui + addi).
static void emit_movi_imm(struct Emitter* e, enum Reg ra, int imm) {
  if (is_encodable_imm(MACHINE_ADD, imm)) {
    emit_alu_rri(e, MACHINE_ADD, ra, R0, imm);
    return;
  }

  if (is_encodable_imm(MACHINE_LUI, imm)) {
    emit_lui(e, ra, imm);
    return;
  }

  struct MachineInstr* instr = emit(e, MACHINE_MOVI);
  instr->instr.movi.ra = ra;
  instr->instr.movi.imm = imm;
}

// Materialize the absolute address label + offset into addr_reg with
// `adpc rA, label + offset` (rA = pc + 4 + imm). The 22-bit signed immediate
// reaches +-2 MiB from the instruction (docs/ISA.md, "adpc").
static void emit_label_address(struct Emitter* e,
                               enum Reg addr_reg,
                               struct Slice* label,
                               int offset) {
  struct MachineInstr* instr = emit(e, MACHINE_ADPC);
  instr->instr.movi.ra = addr_reg;
  instr->instr.movi.label = label;
  instr->instr.movi.imm = offset;
}

// Label-or-immediate payload shared by calls, jumps, branches, and directives.
static void emit_target(struct Emitter* e, enum MachineInstrType type,
                        struct Slice* label, int imm) {
  struct MachineInstr* instr = emit(e, type);
  instr->instr.target.label = label;
  instr->instr.target.imm = imm;
}

// Assembler comment line; text is not copied and must outlive the machine program.
static void emit_comment(struct Emitter* e, struct Slice* text) {
  emit(e, MACHINE_COMMENT)->instr.comment.text = text;
}

// Label definition at the current position.
static void emit_label(struct Emitter* e, struct Slice* name) {
  emit(e, MACHINE_LABEL)->instr.label.name = name;
}

// Source-line marker consumed by the assembler's debug-info output.
static void emit_debug_loc(struct Emitter* e, const char* loc) {
  emit(e, MACHINE_DEBUG_LOC)->instr.debug_loc.loc = loc;
}

// ---------------------------------------------------------------------------
// Opcode selection
// ---------------------------------------------------------------------------

// Select the load opcode for an access of the given width and addressing form.
static enum MachineInstrType load_op(const struct Emitter* e,
                                     const struct AsmType* type,
                                     enum MemForm form) {
  bool label = form == MEM_LABEL;
  switch (type->type) {
    case BYTE:
      return label ? MACHINE_LB : MACHINE_LBA;
    case DOUBLE:
      return label ? MACHINE_LD : MACHINE_LDA;
    case WORD:
      return label ? MACHINE_LW : MACHINE_LWA;
    default:
      codegen_errorf(e, "unsupported asm type %d for load; expected BYTE, DOUBLE, or WORD",
                     (int)type->type);
  }
  return MACHINE_LWA;
}

// Select the store opcode for an access of the given width and addressing form.
static enum MachineInstrType store_op(const struct Emitter* e,
                                      const struct AsmType* type,
                                      enum MemForm form) {
  bool label = form == MEM_LABEL;
  switch (type->type) {
    case BYTE:
      return label ? MACHINE_SB : MACHINE_SBA;
    case DOUBLE:
      return label ? MACHINE_SD : MACHINE_SDA;
    case WORD:
      return label ? MACHINE_SW : MACHINE_SWA;
    default:
      codegen_errorf(e, "unsupported asm type %d for store; expected BYTE, DOUBLE, or WORD",
                     (int)type->type);
  }
  return MACHINE_SWA;
}

// Select the push macro for a value of the given width.
static enum MachineInstrType push_op(const struct Emitter* e, const struct AsmType* type) {
  switch (type->type) {
    case BYTE:
      return MACHINE_PUSHB;
    case DOUBLE:
      return MACHINE_PUSHD;
    case WORD:
      return MACHINE_PUSH;
    default:
      codegen_errorf(e, "unsupported asm type %d for push; expected BYTE, DOUBLE, or WORD",
                     (int)type->type);
  }
  return MACHINE_PUSH;
}

// Select the short conditional branch that tests flags set by a preceding cmp.
static enum MachineInstrType cond_branch_op(const struct Emitter* e, enum TACCondition cond) {
  switch (cond) {
    case CondE:  return MACHINE_BZ;
    case CondNE: return MACHINE_BNZ;
    case CondG:  return MACHINE_BG;
    case CondGE: return MACHINE_BGE;
    case CondL:  return MACHINE_BL;
    case CondLE: return MACHINE_BLE;
    case CondA:  return MACHINE_BA;
    case CondAE: return MACHINE_BAE;
    case CondB:  return MACHINE_BB;
    case CondBE: return MACHINE_BBE;
  }
  codegen_errorf(e, "unknown condition %d; expected TAC CondE..CondBE", (int)cond);
  return MACHINE_BR;
}

// ---------------------------------------------------------------------------
// Operand access
// ---------------------------------------------------------------------------

// The scratch register other than keep_reg: scratch A unless keep_reg is A.
// Pass R0 (or any non-scratch register) when no scratch value is live
static enum Reg pick_scratch_reg(enum Reg keep_reg) {
  return keep_reg == kScratchRegA ? kScratchRegB : kScratchRegA;
}

// Report a Memory operand whose base is a scratch register. Lowerings borrow
// the scratch registers as address temporaries, so such an operand could be
// overwritten before it is used; asm_gen must never produce one.
static void check_memory_base(const struct Emitter* e, const struct Operand* opr) {
  enum Reg base = opr->op.memory.base;
  if (base == kScratchRegA || base == kScratchRegB) {
    codegen_errorf(e, "Memory operand uses scratch register r%d as its base (offset %d); "
                   "scratch registers are reserved for codegen temporaries",
                   (int)base, opr->op.memory.offset);
  }
}

// Load [base, offset] into dst_reg. Frame and aggregate offsets can exceed the
// scaled 12-bit absolute-addressing field; those first form base + offset in
// the scratch register other than keep_reg, which may be dst_reg itself.
// keep_reg is the scratch register holding a value that must survive, R0 if
// none; when base is a scratch register, it must be keep_reg.
static void emit_load_base(struct Emitter* e, enum MachineInstrType type, enum Reg dst_reg,
                           enum Reg base, int offset, enum Reg keep_reg) {
  if (is_encodable_imm(type, offset)) {
    emit_mem_base(e, type, dst_reg, base, offset);
    return;
  }
  enum Reg addr_reg = pick_scratch_reg(keep_reg);
  emit_movi_imm(e, addr_reg, offset);
  emit_alu_rrr(e, MACHINE_ADD, addr_reg, base, addr_reg);
  emit_mem_base(e, type, dst_reg, addr_reg, 0);
}

// Store value_reg to [base, offset]. Offsets the absolute-addressing field
// cannot hold go through the scratch register other than value_reg, so base
// must not be a scratch register and no other scratch value may be live.
static void emit_store_base(struct Emitter* e, enum MachineInstrType type, enum Reg value_reg,
                            enum Reg base, int offset) {
  if (is_encodable_imm(type, offset)) {
    emit_mem_base(e, type, value_reg, base, offset);
    return;
  }
  enum Reg addr_reg = pick_scratch_reg(value_reg);
  emit_movi_imm(e, addr_reg, offset);
  emit_alu_rrr(e, MACHINE_ADD, addr_reg, base, addr_reg);
  emit_mem_base(e, type, value_reg, addr_reg, 0);
}

// Load opr's value into dst_reg. keep_reg names a register holding a live
// value that must survive (R0 if none). A Memory operand with a far offset
// needs an address temporary, a scratch register that avoids keep_reg; Data
// operands are a single PC-relative load and use no temporary.
static void load_operand(struct Emitter* e, const struct Operand* opr,
                         enum Reg dst_reg, enum Reg keep_reg) {
  switch (opr->type) {
    case OPERAND_REG:
      emit_reg2(e, MACHINE_MOV, dst_reg, opr->op.reg.reg);
      return;
    case OPERAND_LIT:
      emit_movi_imm(e, dst_reg, opr->op.lit.value);
      return;
    case OPERAND_MEMORY:
      check_memory_base(e, opr);
      emit_load_base(e, load_op(e, opr->asm_type, MEM_BASE_OFFSET), dst_reg,
                     opr->op.memory.base, opr->op.memory.offset, keep_reg);
      return;
    case OPERAND_DATA:
      emit_mem_label(e, load_op(e, opr->asm_type, MEM_LABEL), dst_reg, opr->op.data.label, opr->op.data.offset);
      return;
    default:
      codegen_errorf(e, "invalid source operand type %d; expected Reg, Lit, Memory, or Data",
                     (int)opr->type);
  }
}

// Store value_reg to a Data operand with a single PC-relative store; no
// scratch register is clobbered.
static void store_to_data(struct Emitter* e, const struct Operand* dst, enum Reg value_reg) {
  emit_mem_label(e, store_op(e, dst->asm_type, MEM_LABEL), value_reg, dst->op.data.label, dst->op.data.offset);
}

// Write value_reg to a destination operand.
static void store_operand(struct Emitter* e, const struct Operand* dst, enum Reg value_reg) {
  switch (dst->type) {
    case OPERAND_REG:
      emit_reg2(e, MACHINE_MOV, dst->op.reg.reg, value_reg);
      return;
    case OPERAND_MEMORY:
      check_memory_base(e, dst);
      emit_store_base(e, store_op(e, dst->asm_type, MEM_BASE_OFFSET), value_reg,
                      dst->op.memory.base, dst->op.memory.offset);
      return;
    case OPERAND_DATA:
      store_to_data(e, dst, value_reg);
      return;
    default:
      codegen_errorf(e, "invalid destination operand type %d; expected Reg, Memory, or Data",
                     (int)dst->type);
  }
}

// ---------------------------------------------------------------------------
// Control-flow sequences
// ---------------------------------------------------------------------------

// Call a two-argument builtin with the ABI argument registers: scratch A and B
// go to r1/r2 and the result comes back in r1, then moves to scratch A.
static void emit_builtin_call(struct Emitter* e, struct Slice* label) {
  emit_reg2(e, MACHINE_MOV, R1, kScratchRegA);
  emit_reg2(e, MACHINE_MOV, R2, kScratchRegB);
  emit_target(e, MACHINE_CALL, label, 0);
  emit_reg2(e, MACHINE_MOV, kScratchRegA, R1);
}

// A leaf function that never touches bp can skip the frame entirely: ra is
// never overwritten and nothing is addressed relative to bp.
static bool function_needs_prologue(const struct AsmFunc* func) {
  return func->makes_calls || func->uses_bp;
}

// Tear down the current frame, leaving the machine as it was just before the
// caller's `call`: sp points at our incoming stack args, bp is the caller's bp,
// and ra holds the caller's return address. Only sp, bp, and ra are written, so
// argument/return registers (r1-r8) survive. The caller of this helper emits
// the final control transfer (ret, or a jump for a tail call). Emits nothing
// for a frameless function, which is already in that state.
static void emit_function_epilogue(struct Emitter* e) {
  if (!function_needs_prologue(e->func)) {
    return;
  }

  emit_comment(e, &kFunctionEpilogueLabel);
  emit_reg2(e, MACHINE_MOV, SP, BP);
  emit_reg1(e, MACHINE_POP, BP);
  emit_reg1(e, MACHINE_POP, RA);
}

// Build the frame: push ra, push bp, point bp at the saved bp, then allocate
// func->frame_bytes of locals below bp. So [bp] holds the caller's bp and
// [bp + 4] the return address; emit_function_epilogue pops them in reverse.
static void emit_function_prologue(struct Emitter* e) {
  if (!function_needs_prologue(e->func)) {
    return;
  }

  emit_comment(e, &kFunctionPrologueLabel);
  emit_reg1(e, MACHINE_PUSH, RA);
  emit_reg1(e, MACHINE_PUSH, BP);
  emit_reg2(e, MACHINE_MOV, BP, SP);

  // use immediate `add` when possible, 
  // otherwise fall back to register sub
  if (e->func->frame_bytes > 0) {
    if (e->func->frame_bytes <= INT_MAX && is_encodable_imm(MACHINE_ADD, -(int)e->func->frame_bytes)) {
      emit_alu_rri(e, MACHINE_ADD, SP, SP, -(int)e->func->frame_bytes);
    } else {
      emit_movi_imm(e, kScratchRegB, (int)e->func->frame_bytes);
      emit_alu_rrr(e, MACHINE_SUB, SP, SP, kScratchRegB);
    }
  }

  // Stack layout comments for user-visible locals (present only with debug info).
  for (struct DebugLocal* local = e->func->locals; local != NULL; local = local->next) {
    struct MachineInstr* instr = emit(e, MACHINE_DEBUG_LOCAL);
    instr->instr.debug_local.name = local->name;
    instr->instr.debug_local.offset = local->offset;
    instr->instr.debug_local.size = local->size;
  }

  emit_comment(e, &kFunctionBodyLabel);
}

// ---------------------------------------------------------------------------
// Instruction lowering
// ---------------------------------------------------------------------------

// Lower a move directly, without staging through scratch registers when either
// side is a register.
static void lower_mov(struct Emitter* e, const struct AsmMov* mov) {
  const struct Operand* dst = mov->dst;
  const struct Operand* src = mov->src;
  if (dst == NULL || src == NULL) {
    codegen_errorf(e, "Mov has a NULL %s operand; asm_gen must set both dst and src",
                   dst == NULL ? "dst" : "src");
  }

  if (dst->type == OPERAND_REG) {
    load_operand(e, src, dst->op.reg.reg, R0);
    return;
  }
  if (src->type == OPERAND_REG) {
    store_operand(e, dst, src->op.reg.reg);
    return;
  }

  // Neither side is a register: stage the value through scratch A. Address
  // bases are never scratch registers, so loading the value cannot clobber
  // the destination's base, and storing it borrows scratch B if needed.
  load_operand(e, src, kScratchRegA, R0);
  store_operand(e, dst, kScratchRegA);
}

// Compute the address of a Memory or Data operand into a register or a word slot.
static void lower_get_address(struct Emitter* e, const struct AsmGetAddress* ga) {
  const struct Operand* dst = ga->dst;
  const struct Operand* src = ga->src;
  if (dst == NULL || src == NULL ||
      (dst->type != OPERAND_REG && dst->type != OPERAND_MEMORY) ||
      (src->type != OPERAND_MEMORY && src->type != OPERAND_DATA)) {
    codegen_errorf(e, "unsupported GetAddress operands (dst=%d, src=%d); "
                   "expected dst=Reg or Memory and src=Memory or Data",
                   dst == NULL ? -1 : (int)dst->type, src == NULL ? -1 : (int)src->type);
  }

  if (src->type == OPERAND_MEMORY) {
    check_memory_base(e, src);
  }
  if (dst->type == OPERAND_MEMORY) {
    check_memory_base(e, dst);
  }

  enum Reg addr_reg = dst->type == OPERAND_REG ? dst->op.reg.reg : kScratchRegB;
  if (src->type == OPERAND_MEMORY) {
    // Unlike loads and stores, `add` does not scale its immediate, so frame
    // offsets beyond 2048 bytes need the offset materialized in a register.
    int offset = src->op.memory.offset;
    enum Reg base = src->op.memory.base;
    if (is_encodable_imm(MACHINE_ADD, offset)) {
      emit_alu_rri(e, MACHINE_ADD, addr_reg, base, offset);
    } else {
      enum Reg offset_reg = pick_scratch_reg(addr_reg);
      emit_movi_imm(e, offset_reg, offset);
      emit_alu_rrr(e, MACHINE_ADD, addr_reg, base, offset_reg);
    }
  } else {
    emit_label_address(e, addr_reg, src->op.data.label, src->op.data.offset);
  }
  if (dst->type == OPERAND_MEMORY) {
    emit_store_base(e, MACHINE_SWA, addr_reg, dst->op.memory.base, dst->op.memory.offset);
  }
}

// Lower a conditional jump to a single immediate branch. Jump targets are
// labels in the same function, and the 22-bit instruction-count offset reaches
// +-8 MiB (docs/ISA.md, "Immediate Branches"); a function too large for that
// fails to assemble (see README, "Address reach limits").
static void lower_cond_jump(struct Emitter* e, const struct AsmCondJump* jump) {
  emit_target(e, cond_branch_op(e, jump->cond), jump->label, 0);
}

// Emit an ALU operation on scratch A and B with the result in scratch A.
// Operations with no single machine instruction call a builtin helper, which
// clobbers all caller-saved registers. The set of builtin ops must match
// alu_op_needs_builtin_call in asm_gen.c, which leaf detection relies on.
static void emit_binary_reg_op(struct Emitter* e, enum ALUOp op) {
  switch (op) {
    case ALU_ADD:
      emit_alu_rrr(e, MACHINE_ADD, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_SUB:
      emit_alu_rrr(e, MACHINE_SUB, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_AND:
      emit_alu_rrr(e, MACHINE_AND, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_OR:
      emit_alu_rrr(e, MACHINE_OR, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_XOR:
      emit_alu_rrr(e, MACHINE_XOR, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_MOV:
      emit_reg2(e, MACHINE_MOV, kScratchRegA, kScratchRegB);
      return;
    case ALU_SMUL: emit_builtin_call(e, &kBuiltinSmul); return;
    case ALU_SDIV: emit_builtin_call(e, &kBuiltinSdiv); return;
    case ALU_SMOD: emit_builtin_call(e, &kBuiltinSmod); return;
    case ALU_UMUL: emit_builtin_call(e, &kBuiltinUmul); return;
    case ALU_UDIV: emit_builtin_call(e, &kBuiltinUdiv); return;
    case ALU_UMOD: emit_builtin_call(e, &kBuiltinUmod); return;
    case ALU_LSL:
    case ALU_ASL:
      emit_alu_rrr(e, MACHINE_LSL, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_LSR:
      emit_alu_rrr(e, MACHINE_LSR, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    case ALU_ASR:
      emit_alu_rrr(e, MACHINE_ASR, kScratchRegA, kScratchRegA, kScratchRegB);
      return;
    default:
      codegen_errorf(e, "unknown ALU op %d; expected a defined ALU_* variant", (int)op);
  }
}

// Choose the machine instruction and immediate that compute `x <op> imm` in
// one instruction. Returns false if op has no immediate form (multiply,
// divide, modulo are builtin calls; ALU_MOV lowers to movi instead) or if imm
// does not fit the chosen instruction's encoding.
static bool binary_imm_form(enum ALUOp op, int imm, enum MachineInstrType* type, int* encoded_imm) {
  *encoded_imm = imm;
  switch (op) {
    case ALU_ADD: *type = MACHINE_ADD; break;
    case ALU_SUB:
      // Immediate `sub` computes `imm - rB` (docs/ISA.md), so subtract by
      // adding the negation, which INT_MIN does not have.
      if (imm == INT_MIN) {
        return false;
      }
      *type = MACHINE_ADD;
      *encoded_imm = -imm;
      break;
    case ALU_AND: *type = MACHINE_AND; break;
    case ALU_OR: *type = MACHINE_OR; break;
    case ALU_XOR: *type = MACHINE_XOR; break;
    // C left shifts are the same bit operation for signed and unsigned values.
    case ALU_LSL:
    case ALU_ASL: *type = MACHINE_LSL; break;
    case ALU_LSR: *type = MACHINE_LSR; break;
    case ALU_ASR: *type = MACHINE_ASR; break;
    case ALU_MOV:
    case ALU_SMUL:
    case ALU_SDIV:
    case ALU_SMOD:
    case ALU_UMUL:
    case ALU_UDIV:
    case ALU_UMOD:
      return false;
  }
  return is_encodable_imm(*type, *encoded_imm);
}

// Return true if `x <op> imm` can be lowered without loading imm into a
// register: through binary_imm_form, or as movi for ALU_MOV.
static bool binary_op_takes_imm(enum ALUOp op, int imm) {
  enum MachineInstrType type;
  int encoded_imm;
  return op == ALU_MOV || binary_imm_form(op, imm, &type, &encoded_imm);
}

// Emit an ALU operation on scratch A and imm with the result in scratch A.
// The caller must have checked binary_op_takes_imm.
static void emit_binary_imm_op(struct Emitter* e, enum ALUOp op, int imm) {
  if (op == ALU_MOV) {
    emit_movi_imm(e, kScratchRegA, imm);
    return;
  }
  enum MachineInstrType type;
  int encoded_imm;
  if (!binary_imm_form(op, imm, &type, &encoded_imm)) {
    codegen_errorf(e, "ALU op %d has no immediate form that encodes %d; "
                   "expected the operand to be loaded into a register", (int)op, imm);
  }
  emit_alu_rri(e, type, kScratchRegA, kScratchRegA, encoded_imm);
}

// Emit the operation step of the scratch template: sources are already in
// scratch A (and B), and the result must be left in scratch A.
static void emit_scratch_op(struct Emitter* e, const struct AsmInstr* cur) {
  switch (cur->type) {
    case ASM_VOLATILE_READ:
    case ASM_VOLATILE_WRITE:
      // A plain copy; the opcode stays distinct only so earlier passes keep the access.
      return;
    case ASM_CMP:
      emit_reg2(e, MACHINE_CMP, kScratchRegA, kScratchRegB);
      return;
    case ASM_UNARY:
      switch (cur->instr.asm_unary.op) {
        case COMPLEMENT:
          emit_reg2(e, MACHINE_NOT, kScratchRegA, kScratchRegA);
          return;
        case NEGATE:
          emit_alu_rrr(e, MACHINE_SUB, kScratchRegA, R0, kScratchRegA);
          return;
        case UNARY_PLUS:
          return;
        default:
          codegen_errorf(e, "unsupported unary op %d; expected COMPLEMENT, NEGATE, or UNARY_PLUS",
                         (int)cur->instr.asm_unary.op);
      }
      return;
    case ASM_BINARY:
      emit_binary_reg_op(e, cur->instr.asm_binary.alu_op);
      return;
    case ASM_PUSH:
      emit_reg1(e, push_op(e, cur->instr.asm_push.src->asm_type), kScratchRegA);
      return;
    case ASM_INDIRECT_CALL:
      emit_reg2(e, MACHINE_BRA, RA, kScratchRegA);
      return;
    case ASM_TAIL_CALL_INDIRECT:
      // The target was read into scratch A before the frame is torn down,
      // because the source operand may be BP-relative.
      emit_function_epilogue(e);
      emit_reg2(e, MACHINE_BRA, R0, kScratchRegA);
      return;
    case ASM_TRUNC:
      if (cur->instr.asm_trunc.size == 1) {
        emit_reg2(e, MACHINE_TNCB, kScratchRegA, kScratchRegA);
      } else if (cur->instr.asm_trunc.size == 2) {
        emit_reg2(e, MACHINE_TNCD, kScratchRegA, kScratchRegA);
      } else {
        codegen_errorf(e, "unsupported truncation size %d; expected 1 or 2",
                       (int)cur->instr.asm_trunc.size);
      }
      return;
    case ASM_EXTEND:
      if (cur->instr.asm_extend.size == 1) {
        emit_reg2(e, MACHINE_SXTB, kScratchRegA, kScratchRegA);
      } else if (cur->instr.asm_extend.size == 2) {
        emit_reg2(e, MACHINE_SXTD, kScratchRegA, kScratchRegA);
      } else {
        codegen_errorf(e, "unsupported extend size %d; expected 1 or 2",
                       (int)cur->instr.asm_extend.size);
      }
      return;
    case ASM_LOAD:
    case ASM_VOLATILE_LOAD: {
      const struct Operand* load_dst = cur->type == ASM_VOLATILE_LOAD
          ? cur->instr.asm_volatile_load.dst
          : cur->instr.asm_load.dst;
      int offset = cur->type == ASM_VOLATILE_LOAD
          ? cur->instr.asm_volatile_load.offset
          : cur->instr.asm_load.offset;
      // The pointer in scratch A is the base, so it is the value to keep.
      emit_load_base(e, load_op(e, load_dst->asm_type, MEM_BASE_OFFSET), kScratchRegA, kScratchRegA,
                     offset, kScratchRegA);
      return;
    }
    case ASM_STORE:
    case ASM_VOLATILE_STORE: {
      // The slot order puts the value first (scratch A) and the address second (scratch B).
      const struct Operand* store_src = cur->type == ASM_VOLATILE_STORE
          ? cur->instr.asm_volatile_store.src
          : cur->instr.asm_store.src;
      int offset = cur->type == ASM_VOLATILE_STORE
          ? cur->instr.asm_volatile_store.offset
          : cur->instr.asm_store.offset;
      // lower_store sends only offsets the immediate field holds here, so
      // the store never needs an address temporary.
      emit_mem_base(e, store_op(e, store_src->asm_type, MEM_BASE_OFFSET), kScratchRegA, kScratchRegB, offset);
      return;
    }
    default:
      codegen_errorf(e, "ASM instruction type %d has no scratch-template lowering", (int)cur->type);
  }
}

// Lower an instruction through the fixed scratch template: load the operands
// it reads into scratch A (and B), emit the operation, then write scratch A to
// the operand it writes.
static void lower_via_scratch(struct Emitter* e, struct AsmInstr* cur) {
  struct OperandSlots slots = asm_operand_slots(cur);
  struct Operand* dst = NULL;
  size_t loaded = 0;

  bool use_imm = false;
  int imm = 0;

  for (size_t i = 0; i < slots.count; i++) {
    struct Operand* opr = *slots.slot[i].field;
    if (slots.slot[i].role == OPERAND_DEF) {
      dst = opr;
    } else if (
      i == 2 && 
      slots.slot[i].role == OPERAND_USE &&
      cur->type == ASM_BINARY &&
      opr->type == OPERAND_LIT &&
      binary_op_takes_imm(cur->instr.asm_binary.alu_op, opr->op.lit.value)) {
      // skip loading, and let the immediate be used directly in the instruction
      use_imm = true;
      imm = opr->op.lit.value;
    } else if (loaded == 0) {
      load_operand(e, opr, kScratchRegA, R0);
      loaded++;
    } else {
      load_operand(e, opr, kScratchRegB, kScratchRegA);
      loaded++;
    }
  }

  if (use_imm) {
    emit_binary_imm_op(e, cur->instr.asm_binary.alu_op, imm);
  } else {
    emit_scratch_op(e, cur);
  }

  if (dst != NULL) {
    store_operand(e, dst, kScratchRegA);
  }
}

// Lower a Store or VolatileStore. An offset the absolute-addressing field can
// hold uses the scratch template. A far offset would leave the value, the
// pointer, and the materialized offset live at once, so this path instead
// folds the offset into the pointer before the value is loaded:
//   A = ptr; B = offset; A = A + B; B = value; store B -> [A, 0]
// which needs only the two scratch registers.
static void lower_store(struct Emitter* e, struct AsmInstr* cur) {
  const struct AsmStore* store = cur->type == ASM_VOLATILE_STORE
      ? &cur->instr.asm_volatile_store
      : &cur->instr.asm_store;
  enum MachineInstrType type = store_op(e, store->src->asm_type, MEM_BASE_OFFSET);
  if (is_encodable_imm(type, store->offset)) {
    lower_via_scratch(e, cur);
    return;
  }
  load_operand(e, store->dst, kScratchRegA, R0);
  emit_movi_imm(e, kScratchRegB, store->offset);
  emit_alu_rrr(e, MACHINE_ADD, kScratchRegA, kScratchRegA, kScratchRegB);
  load_operand(e, store->src, kScratchRegB, kScratchRegA);
  emit_mem_base(e, type, kScratchRegB, kScratchRegA, 0);
}

// Lower one ASM instruction to machine instructions.
static void lower_instr(struct Emitter* e, struct AsmInstr* cur) {
  e->cur = cur;
  switch (cur->type) {
    case ASM_MOV:
      lower_mov(e, &cur->instr.asm_mov);
      break;
    case ASM_GET_ADDRESS:
      lower_get_address(e, &cur->instr.asm_get_address);
      break;
    case ASM_LABEL:
      emit_label(e, cur->instr.asm_label.label);
      break;
    case ASM_JUMP:
      // Same +-8 MiB reach as lower_cond_jump.
      emit_target(e, MACHINE_JMP, cur->instr.asm_jump.label, 0);
      break;
    case ASM_COND_JUMP:
      lower_cond_jump(e, &cur->instr.asm_cond_jump);
      break;
    case ASM_CALL:
      emit_target(e, MACHINE_CALL, cur->instr.asm_call.label, 0);
      break;
    case ASM_TAIL_CALL:
      emit_function_epilogue(e);
      emit_target(e, MACHINE_JMP, cur->instr.asm_tail_call.label, 0);
      break;
    case ASM_RET:
      emit_function_epilogue(e);
      emit(e, MACHINE_RET);
      break;
    case ASM_BOUNDARY:
      if (cur->instr.asm_boundary.loc != NULL) {
        emit_debug_loc(e, cur->instr.asm_boundary.loc);
      }
      break;
    case ASM_STORE:
    case ASM_VOLATILE_STORE:
      lower_store(e, cur);
      break;
    case ASM_VOLATILE_READ:
    case ASM_VOLATILE_WRITE:
    case ASM_VOLATILE_LOAD:
    case ASM_UNARY:
    case ASM_BINARY:
    case ASM_CMP:
    case ASM_PUSH:
    case ASM_INDIRECT_CALL:
    case ASM_TAIL_CALL_INDIRECT:
    case ASM_LOAD:
    case ASM_TRUNC:
    case ASM_EXTEND:
      lower_via_scratch(e, cur);
      break;
    default:
      codegen_errorf(e, "unknown ASM instruction type %d", (int)cur->type);
  }
  e->cur = NULL;
}

// ---------------------------------------------------------------------------
// Top-level lowering
// ---------------------------------------------------------------------------

// Find the first source location marker in a function body, or NULL if none.
static const char* find_function_entry_loc(const struct AsmInstr* instrs) {
  for (const struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) {
    if (cur->type == ASM_BOUNDARY) {
      return cur->instr.asm_boundary.loc;
    }
  }
  return NULL;
}

// Emit a function: optional .global, label, entry line marker, prologue, then the body.
static void lower_function(struct Emitter* e, const struct AsmFunc* func) {
  e->func = func;

  emit(e, MACHINE_NEWLINE);
  if (func->global) {
    emit_target(e, MACHINE_GLOBAL, func->name, 0);
  }
  emit_label(e, func->name);

  // A line marker at the label keeps debugger locations valid at function entry.
  const char* entry_loc = find_function_entry_loc(func->body);
  if (entry_loc != NULL) {
    emit_debug_loc(e, entry_loc);
  }

  emit_function_prologue(e);
  for (struct AsmInstr* cur = func->body; cur != NULL; cur = cur->next) {
    lower_instr(e, cur);
  }

  e->func = NULL;
}

// Emit data directives for a static initializer list. A NULL list is a
// tentative definition and becomes zero-filled storage of the full symbol size.
static void emit_static_data(struct Emitter* e, const struct InitList* init, struct AsmType* type) {
  if (init == NULL) {
    emit_target(e, MACHINE_SPACE, NULL, (int)asm_type_size(type));
    return;
  }

  for (const struct InitList* cur = init; cur != NULL; cur = cur->next) {
    const struct StaticInit* value = cur->value;
    switch (value->int_type) {
      case CHAR_INIT:
      case UCHAR_INIT:
        emit_target(e, MACHINE_FILB, NULL, (int)value->value.num);
        break;
      case SHORT_INIT:
      case USHORT_INIT:
        emit_target(e, MACHINE_FILD, NULL, (int)value->value.num);
        break;
      case INT_INIT:
      case UINT_INIT:
        emit_target(e, MACHINE_FILL, NULL, (int)value->value.num);
        break;
      case POINTER_INIT: {
        // A NULL label is a null pointer constant; emit its numeric value instead.
        struct Slice* label = value->value.pointer;
        emit_target(e, MACHINE_FILL, label, label == NULL ? (int)value->value.num : 0);
        break;
      }
      case LONG_INIT:
      case ULONG_INIT: {
        // 64-bit values are two words, low word first (little-endian).
        uint64_t raw = value->value.num;
        emit_target(e, MACHINE_FILL, NULL, (int32_t)(uint32_t)(raw & 0xFFFFFFFFu));
        emit_target(e, MACHINE_FILL, NULL, (int32_t)(uint32_t)((raw >> 32) & 0xFFFFFFFFu));
        break;
      }
      case ZERO_INIT:
        emit_target(e, MACHINE_SPACE, NULL, (int)value->value.num);
        break;
      case STRING_INIT:
        // One byte per character; padding and the terminator come from ZERO_INIT entries.
        for (size_t i = 0; i < value->value.string->len; i++) {
          emit_target(e, MACHINE_FILB, NULL, (int)(unsigned char)value->value.string->start[i]);
        }
        break;
      default:
        codegen_errorf(e, "unsupported static initializer type %d", (int)value->int_type);
    }
  }
}

// Emit an aligned, labeled static object. Alignment and size come from the ASM
// symbol table entry, which asm_gen must have created for every static object.
static void lower_static_object(struct Emitter* e, struct Slice* name, bool global,
                                const struct InitList* init) {
  struct AsmSymbolEntry* sym_entry = asm_symbol_table_get(asm_symbol_table, name);
  if (sym_entry == NULL) {
    codegen_errorf(e, "static object '%.*s' is missing from the ASM symbol table",
                   (int)name->len, name->start);
  }

  emit_target(e, MACHINE_ALIGN, NULL, (int)asm_type_alignment(sym_entry->type));
  if (global) {
    emit_target(e, MACHINE_GLOBAL, name, 0);
  }
  emit_label(e, name);
  emit_static_data(e, init, sym_entry->type);
}

// Lower one ASM top-level item (function, static object, or directive).
static void lower_top_level(struct Emitter* e, const struct AsmTopLevel* top) {
  switch (top->type) {
    case ASM_FUNC:
      lower_function(e, &top->top.asm_func);
      return;
    case ASM_STATIC_VAR:
      lower_static_object(e, top->top.asm_static_var.name, top->top.asm_static_var.global,
                          top->top.asm_static_var.init_values);
      return;
    case ASM_STATIC_CONST:
      lower_static_object(e, top->top.asm_static_const.name, top->top.asm_static_const.global,
                          top->top.asm_static_const.init_values);
      return;
    case ASM_SECTION:
      emit_target(e, MACHINE_SECTION, top->top.asm_section.name, 0);
      return;
    case ASM_ALIGN:
      emit_target(e, MACHINE_ALIGN, NULL, top->top.asm_align.alignment);
      return;
  }
  codegen_errorf(e, "unknown ASM top-level type %d", (int)top->type);
}

struct MachineProg* prog_to_machine(struct AsmProg* asm_prog) {
  struct Emitter e = {0};
  for (struct AsmTopLevel* top = asm_prog->head; top != NULL; top = top->next) {
    lower_top_level(&e, top);
  }

  struct MachineProg* machine_prog = arena_alloc(sizeof(struct MachineProg));
  machine_prog->head = e.head;
  machine_prog->tail = e.tail;
  return machine_prog;
}
