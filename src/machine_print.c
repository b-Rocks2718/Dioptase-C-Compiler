#include "machine_print.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "asm_gen.h"
#include "slice.h"
#include "source_location.h"

// Write a slice to a file without assuming NUL termination.
// out is the file to write to; slice may be NULL.
static void write_slice(FILE* out, const struct Slice* slice) {
  if (slice == NULL) {
    fputs("<null>", out);
    return;
  }
  fwrite(slice->start, 1, slice->len, out);
}

// Write a register name in assembler syntax.
static void write_reg(FILE* out, enum Reg reg) {
  switch (reg) {
    case R29:
      fputs("ra", out);
      return;
    case R30:
      fputs("bp", out);
      return;
    case R31:
      fputs("sp", out);
      return;
    default:
      fprintf(out, "r%u", (unsigned)reg);
      return;
  }
}

// Write `label`, `label + imm`, or `label - imm` when label is non-NULL (the
// offset is omitted when zero), otherwise imm as a decimal literal.
static void write_label_or_imm(FILE* out, const struct Slice* label, int imm) {
  if (label != NULL) {
    write_slice(out, label);
    if (imm != 0) {
      if (imm > 0) {
        fprintf(out, " + %d", imm);
      } else {
        // Negate in unsigned arithmetic so INT_MIN does not overflow.
        fprintf(out, " - %u", 0u - (unsigned)imm);
      }
    }
  } else {
    fprintf(out, "%d", imm);
  }
}

// Write a memory operand in assembler syntax.
static void write_mem_operand(FILE* out, enum Reg base, int imm) {
  fputc('[', out);
  write_reg(out, base);
  fprintf(out, ", %d]", imm);
}

// Write a memory operand that may use a label for PC-relative addressing.
static void write_mem_operand_label(FILE* out, enum Reg base, const struct Slice* label, int imm) {
  fputc('[', out);
  if (base != R0) {
    write_reg(out, base);
    fputs(", ", out);
  }
  write_label_or_imm(out, label, imm);
  fputc(']', out);
}

// Emit a leading tab for non-label lines.
static void write_tab(FILE* out) {
  fputc('\t', out);
}

// How an opcode's operands are printed after its mnemonic.
enum MachineFormat {
  FMT_NONE = 0,   // not in the table: printed by a special case below
  FMT_BARE,       // mn
  FMT_REG1,       // mn ra
  FMT_REG2,       // mn ra rb
  FMT_REG2_COMMA, // mn ra, rb            (branch through register)
  FMT_ALU,        // mn ra rb rc|imm      (imm form when imm != 0)
  FMT_MOVI,       // mn ra label|imm
  FMT_LUI,        // mn ra imm
  FMT_MEM_BASE,   // mn ra, [rb, imm]
  FMT_MEM_LABEL,  // mn ra, [label] / [rb, label] / [rb, imm]
  FMT_TARGET,     // mn label|imm         (branches, call, jmp, .fill)
  FMT_LABEL,      // mn label             (.global)
  FMT_IMM,        // mn imm               (.fild, .filb, .space, .align)
};

// Mnemonic and operand format of every opcode printed uniformly.
static const struct {
  const char* mnemonic;
  enum MachineFormat format;
} kMachineOps[] = {
  [MACHINE_GLOBAL] = {".global", FMT_LABEL},
  [MACHINE_FILL] = {".fill", FMT_TARGET},
  [MACHINE_FILD] = {".fild", FMT_IMM},
  [MACHINE_FILB] = {".filb", FMT_IMM},
  [MACHINE_SPACE] = {".space", FMT_IMM},
  [MACHINE_ALIGN] = {".align", FMT_IMM},
  [MACHINE_MOV] = {"mov", FMT_REG2},
  [MACHINE_MOVI] = {"movi", FMT_MOVI},
  [MACHINE_LUI] = {"lui", FMT_LUI},
  [MACHINE_AND] = {"and", FMT_ALU},
  [MACHINE_NAND] = {"nand", FMT_ALU},
  [MACHINE_OR] = {"or", FMT_ALU},
  [MACHINE_NOR] = {"nor", FMT_ALU},
  [MACHINE_XOR] = {"xor", FMT_ALU},
  [MACHINE_XNOR] = {"xnor", FMT_ALU},
  [MACHINE_ADD] = {"add", FMT_ALU},
  [MACHINE_ADDC] = {"addc", FMT_ALU},
  [MACHINE_SUB] = {"sub", FMT_ALU},
  [MACHINE_SUBB] = {"subb", FMT_ALU},
  [MACHINE_NOT] = {"not", FMT_REG2},
  [MACHINE_LSL] = {"lsl", FMT_ALU},
  [MACHINE_LSR] = {"lsr", FMT_ALU},
  [MACHINE_ASR] = {"asr", FMT_ALU},
  [MACHINE_ROTL] = {"rotl", FMT_ALU},
  [MACHINE_ROTR] = {"rotr", FMT_ALU},
  [MACHINE_LSLC] = {"lslc", FMT_ALU},
  [MACHINE_LSRC] = {"lsrc", FMT_ALU},
  [MACHINE_EXTEND_B] = {"extend_b", FMT_REG2},
  [MACHINE_EXTEND_D] = {"extend_d", FMT_REG2},
  [MACHINE_TRUNCATE_B] = {"truncate_b", FMT_REG2},
  [MACHINE_TRUNCATE_D] = {"truncate_d", FMT_REG2},
  [MACHINE_SWA] = {"swa", FMT_MEM_BASE},
  [MACHINE_LWA] = {"lwa", FMT_MEM_BASE},
  [MACHINE_SW] = {"sw", FMT_MEM_LABEL},
  [MACHINE_LW] = {"lw", FMT_MEM_LABEL},
  [MACHINE_SDA] = {"sda", FMT_MEM_BASE},
  [MACHINE_LDA] = {"lda", FMT_MEM_BASE},
  [MACHINE_SD] = {"sd", FMT_MEM_LABEL},
  [MACHINE_LD] = {"ld", FMT_MEM_LABEL},
  [MACHINE_SBA] = {"sba", FMT_MEM_BASE},
  [MACHINE_LBA] = {"lba", FMT_MEM_BASE},
  [MACHINE_SB] = {"sb", FMT_MEM_LABEL},
  [MACHINE_LB] = {"lb", FMT_MEM_LABEL},
  [MACHINE_BR] = {"br", FMT_REG2_COMMA},
  [MACHINE_BZ] = {"bz", FMT_TARGET},
  [MACHINE_BNZ] = {"bnz", FMT_TARGET},
  [MACHINE_BS] = {"bs", FMT_TARGET},
  [MACHINE_BNS] = {"bns", FMT_TARGET},
  [MACHINE_BC] = {"bc", FMT_TARGET},
  [MACHINE_BNC] = {"bnc", FMT_TARGET},
  [MACHINE_BO] = {"bo", FMT_TARGET},
  [MACHINE_BNO] = {"bno", FMT_TARGET},
  [MACHINE_BPS] = {"bps", FMT_TARGET},
  [MACHINE_BNPS] = {"bnps", FMT_TARGET},
  [MACHINE_BG] = {"bg", FMT_TARGET},
  [MACHINE_BGE] = {"bge", FMT_TARGET},
  [MACHINE_BL] = {"bl", FMT_TARGET},
  [MACHINE_BLE] = {"ble", FMT_TARGET},
  [MACHINE_BA] = {"ba", FMT_TARGET},
  [MACHINE_BAE] = {"bae", FMT_TARGET},
  [MACHINE_BB] = {"bb", FMT_TARGET},
  [MACHINE_BBE] = {"bbe", FMT_TARGET},
  [MACHINE_BRA] = {"bra", FMT_REG2_COMMA},
  [MACHINE_BZA] = {"bza", FMT_REG2_COMMA},
  [MACHINE_BNZA] = {"bnza", FMT_REG2_COMMA},
  [MACHINE_BSA] = {"bsa", FMT_REG2_COMMA},
  [MACHINE_BNSA] = {"bnsa", FMT_REG2_COMMA},
  [MACHINE_BCA] = {"bca", FMT_REG2_COMMA},
  [MACHINE_BNCA] = {"bnca", FMT_REG2_COMMA},
  [MACHINE_BOA] = {"boa", FMT_REG2_COMMA},
  [MACHINE_BNOA] = {"bnoa", FMT_REG2_COMMA},
  [MACHINE_BPSA] = {"bpa", FMT_REG2_COMMA},
  [MACHINE_BNPSA] = {"bnpa", FMT_REG2_COMMA},
  [MACHINE_BGA] = {"bga", FMT_REG2_COMMA},
  [MACHINE_BGEA] = {"bgea", FMT_REG2_COMMA},
  [MACHINE_BLA] = {"bla", FMT_REG2_COMMA},
  [MACHINE_BLEA] = {"blea", FMT_REG2_COMMA},
  [MACHINE_BAA] = {"baa", FMT_REG2_COMMA},
  [MACHINE_BAEA] = {"baea", FMT_REG2_COMMA},
  [MACHINE_BBA] = {"bba", FMT_REG2_COMMA},
  [MACHINE_BBEA] = {"bbea", FMT_REG2_COMMA},
  [MACHINE_SYS] = {"trap", FMT_BARE},
  [MACHINE_NOP] = {"nop", FMT_BARE},
  [MACHINE_PUSH] = {"push", FMT_REG1},
  [MACHINE_POP] = {"pop", FMT_REG1},
  [MACHINE_PUSHD] = {"pshd", FMT_REG1},
  [MACHINE_POPD] = {"popd", FMT_REG1},
  [MACHINE_PUSHB] = {"pshb", FMT_REG1},
  [MACHINE_POPB] = {"popb", FMT_REG1},
  [MACHINE_CALL] = {"call", FMT_TARGET},
  [MACHINE_RET] = {"ret", FMT_BARE},
  [MACHINE_JMP] = {"jmp", FMT_TARGET},
  [MACHINE_CMP] = {"cmp", FMT_REG2},
  [MACHINE_TNCB] = {"tncb", FMT_REG2},
  [MACHINE_TNCD] = {"tncd", FMT_REG2},
  [MACHINE_SXTB] = {"sxtb", FMT_REG2},
  [MACHINE_SXTD] = {"sxtd", FMT_REG2},
  [MACHINE_ADPC] = {"adpc", FMT_MOVI},
};

// Print an opcode from kMachineOps. Returns false if it is not in the table.
static bool write_table_instr(FILE* out, const struct MachineInstr* instr) {
  if ((size_t)instr->type >= sizeof(kMachineOps) / sizeof(kMachineOps[0]) ||
      kMachineOps[instr->type].format == FMT_NONE) {
    return false;
  }
  const char* mnemonic = kMachineOps[instr->type].mnemonic;
  const union MachineInstrVariant* in = &instr->instr;
  write_tab(out);
  fputs(mnemonic, out);
  switch (kMachineOps[instr->type].format) {
    case FMT_NONE:
    case FMT_BARE:
      break;
    case FMT_REG1:
      fputc(' ', out);
      write_reg(out, in->reg.ra);
      break;
    case FMT_REG2:
    case FMT_REG2_COMMA:
      fputc(' ', out);
      write_reg(out, in->reg2.ra);
      fputs(kMachineOps[instr->type].format == FMT_REG2 ? " " : ", ", out);
      write_reg(out, in->reg2.rb);
      break;
    case FMT_ALU:
      fputc(' ', out);
      write_reg(out, in->alu.ra);
      fputc(' ', out);
      write_reg(out, in->alu.rb);
      fputc(' ', out);
      if (in->alu.imm == 0) {
        write_reg(out, in->alu.rc);
      } else {
        fprintf(out, "%d", in->alu.imm);
      }
      break;
    case FMT_MOVI:
      fputc(' ', out);
      write_reg(out, in->movi.ra);
      fputc(' ', out);
      write_label_or_imm(out, in->movi.label, in->movi.imm);
      break;
    case FMT_LUI:
      fputc(' ', out);
      write_reg(out, in->movi.ra);
      fprintf(out, " %d", in->movi.imm);
      break;
    case FMT_MEM_BASE:
      fputc(' ', out);
      write_reg(out, in->mem.ra);
      fputs(", ", out);
      write_mem_operand(out, in->mem.rb, in->mem.imm);
      break;
    case FMT_MEM_LABEL:
      fputc(' ', out);
      write_reg(out, in->mem.ra);
      fputs(", ", out);
      if (in->mem.label == NULL) {
        write_mem_operand(out, in->mem.rb, in->mem.imm);
      } else if (in->mem.rb == R0 && in->mem.imm == 0) {
        fputc('[', out);
        write_slice(out, in->mem.label);
        fputc(']', out);
      } else {
        write_mem_operand_label(out, in->mem.rb, in->mem.label, in->mem.imm);
      }
      break;
    case FMT_TARGET:
      fputc(' ', out);
      write_label_or_imm(out, in->target.label, in->target.imm);
      break;
    case FMT_LABEL:
      fputc(' ', out);
      write_slice(out, in->target.label);
      break;
    case FMT_IMM:
      fprintf(out, " %d", in->target.imm);
      break;
  }
  fputc('\n', out);
  return true;
}

// Emit a single machine instruction as assembly text.
// Returns false if an unknown instruction is encountered.
static bool write_machine_instr(FILE* out, const struct MachineInstr* instr) {
  if (write_table_instr(out, instr)) {
    return true;
  }
  switch (instr->type) {
    case MACHINE_LABEL:
      write_slice(out, instr->instr.label.name);
      fputs(":\n", out);
      return true;
    case MACHINE_DEBUG_LOC: {
      if (instr->instr.debug_loc.loc == NULL) {
        return true;
      }
      struct SourceLocation loc = source_location_from_ptr(instr->instr.debug_loc.loc);
      const char* filename = source_filename_for_ptr(instr->instr.debug_loc.loc);
      write_tab(out);
      fprintf(out, ".line %s %zu\n", filename, loc.line);
      return true;
    }
    case MACHINE_DEBUG_LOCAL:
      if (instr->instr.debug_local.name == NULL) {
        return true;
      }
      write_tab(out);
      fputs(".local ", out);
      write_slice(out, instr->instr.debug_local.name);
      // BP-relative offset, then size in bytes.
      fprintf(out, " %d %d\n", instr->instr.debug_local.offset, instr->instr.debug_local.size);
      return true;
    case MACHINE_COMMENT:
      write_tab(out);
      fputs("# ", out);
      write_slice(out, instr->instr.comment.text);
      fputc('\n', out);
      return true;
    case MACHINE_NLCOMMENT:
      fputc('\n', out);
      write_tab(out);
      fputs("# ", out);
      write_slice(out, instr->instr.comment.text);
      fputc('\n', out);
      return true;
    case MACHINE_NEWLINE:
      fputc('\n', out);
      return true;
    case MACHINE_SECTION:
      write_tab(out);
      fputs("\n\t.", out);
      write_slice(out, instr->instr.target.label);
      fputc('\n', out);
      return true;
    default:
      fprintf(stderr, "Compiler Error: machine_print: unknown instruction type %d\n",
              (int)instr->type);
      return false;
  }
}

// Serialize the complete machine program as assembler source.
bool write_machine_prog_to_file(const struct MachineProg* prog, const char* path) {
  if (prog == NULL || path == NULL) {
    fprintf(stderr, "Compiler Error: machine_print: invalid write request\n");
    return false;
  }

  FILE* out = fopen(path, "w");
  if (out == NULL) {
    fprintf(stderr, "Compiler Error: machine_print: failed to open %s: %s\n",
            path, strerror(errno));
    return false;
  }

  for (const struct MachineInstr* cur = prog->head; cur != NULL; cur = cur->next) {
    if (!write_machine_instr(out, cur)) {
      fclose(out);
      return false;
    }
  }

  if (fclose(out) != 0) {
    fprintf(stderr, "Compiler Error: machine_print: failed to close %s: %s\n",
            path, strerror(errno));
    return false;
  }

  return true;
}
