#include <inttypes.h>
#include <stdio.h>

#include "asm_gen.h"
#include "slice.h"
#include "typechecking.h"
#include "source_location.h"
#include "TAC.h"

// Provide human-readable printing of ASM IR for debugging.

// Emit indentation for ASM output formatting.
static void print_tabs(unsigned tabs) {
  static const char kIndent[] = "    ";
  for (unsigned i = 0; i < tabs; ++i) {
    fputs(kIndent, stdout);
  }
}

// Convert a register enum to its canonical name.
// Returns a string literal describing the register.
static const char* reg_name(enum Reg reg) {
  static const char* kRegNames[] = {
      "R0",  "R1",  "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
      "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15",
      "R16", "R17", "R18", "R19", "R20", "R21", "R22", "R23",
      "R24", "R25", "R26", "R27", "R28", "R29", "R30", "R31"};
  const size_t reg_count = sizeof(kRegNames) / sizeof(kRegNames[0]);
  size_t idx = (size_t)reg;
  if (idx >= reg_count) {
    return "R?";
  }
  return kRegNames[idx];
}

// Print a register with its architectural alias (if any).
static void print_reg(enum Reg reg) {
  printf("%s", reg_name(reg));
  if (reg == BP) {
    printf("(BP)");
  } else if (reg == SP) {
    printf("(SP)");
  } else if (reg == RA) {
    printf("(RA)");
  }
}

// Render an assembly operand type, including byte-array size and alignment.
static void print_asm_type(struct AsmType* type) {
  switch (type->type) {
    case BYTE:
      printf("BYTE");
      break;
    case DOUBLE:
      printf("DOUBLE");
      break;
    case WORD:
      printf("WORD");
      break;
    case LONG_WORD:
      printf("LONG_WORD");
      break;
    case BYTE_ARRAY:
      printf("BYTE_ARRAY(size=%zu, alignment=%zu)", type->byte_array.size, type->byte_array.alignment);
      break;
    default:
      printf("ASMType<%d>?", (int)type->type);
      break;
  }
}

// Print an ASM operand in a compact readable form.
// opr is the operand to print (may be NULL).
static void print_operand(const struct Operand* opr) {
  if (opr == NULL) {
    printf("<null>");
    return;
  }

  switch (opr->type) {
    case OPERAND_LIT:
      printf("Lit(%d)", opr->op.lit.value);
      break;
    case OPERAND_REG:
      printf("Reg(");
      print_reg(opr->op.reg.reg);
      printf(")");
      break;
    case OPERAND_PSEUDO:
      printf("Pseudo(");
      print_slice(opr->op.pseudo.name);
      printf(")");
      break;
    case OPERAND_PSEUDO_MEM:
      printf("PseudoMem(");
      print_slice(opr->op.pseudo_mem.name);
      printf(", %d)", opr->op.pseudo_mem.offset);
      break;
    case OPERAND_MEMORY:
      printf("Mem(");
      print_reg(opr->op.memory.base);
      printf(", %d, ", opr->op.memory.offset);
      print_asm_type(opr->asm_type);
      printf(")");
      break;
    case OPERAND_DATA:
      printf("Data(");
      print_slice(opr->op.data.label);
      printf(")");
      break;
    default:
      printf("Operand(?)");
      break;
  }
}

// Print a TAC condition mnemonic used by ASM conditional jumps.
static void print_asm_condition(enum TACCondition cond) {
  switch (cond) {
    case CondE:
      printf("CondE");
      break;
    case CondNE:
      printf("CondNE");
      break;
    case CondG:
      printf("CondG");
      break;
    case CondGE:
      printf("CondGE");
      break;
    case CondL:
      printf("CondL");
      break;
    case CondLE:
      printf("CondLE");
      break;
    case CondA:
      printf("CondA");
      break;
    case CondAE:
      printf("CondAE");
      break;
    case CondB:
      printf("CondB");
      break;
    case CondBE:
      printf("CondBE");
      break;
    default:
      printf("Cond?");
      break;
  }
}

// Print a unary operator mnemonic used in ASM IR.
static void print_asm_un_op(enum UnOp op) {
  switch (op) {
    case COMPLEMENT:
      printf("Complement");
      break;
    case NEGATE:
      printf("Negate");
      break;
    case BOOL_NOT:
      printf("BoolNot");
      break;
    case UNARY_PLUS:
      printf("UnaryPlus");
      break;
    default:
      printf("UnOp?");
      break;
  }
}

// Print a binary ALU operator mnemonic used in ASM IR.
static void print_asm_alu_op(enum ALUOp op) {
  switch (op) {
    case ALU_ADD:
      printf("AddOp");
      break;
    case ALU_SUB:
      printf("SubOp");
      break;
    case ALU_SMUL:
      printf("SmulOp");
      break;
    case ALU_SDIV:
      printf("SDivOp");
      break;
    case ALU_SMOD:
      printf("SModOp");
      break;
    case ALU_UMUL:
      printf("UMulOp");
      break;
    case ALU_UDIV:
      printf("UDivOp");
      break;
    case ALU_UMOD:
      printf("UModOp");
      break;
    case ALU_AND:
      printf("AndOp");
      break;
    case ALU_OR:
      printf("OrOp");
      break;
    case ALU_XOR:
      printf("XorOp");
      break;
    case ALU_LSR:
      printf("LsrOp");
      break;
    case ALU_LSL:
      printf("LslOp");
      break;
    case ALU_ASR:
      printf("AsrOp");
      break;
    case ALU_MOV:
      printf("MovOp");
      break;
    default:
      printf("ALUOp?");
      break;
  }
}

// Print a single ASM instruction at a given indentation level.
static void print_asm_instr(const struct AsmInstr* instr, unsigned tabs) {
  if (instr == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (instr->type) {
    case ASM_MOV:
      printf("Mov ");
      print_operand(instr->instr.asm_mov.dst);
      printf(", ");
      print_operand(instr->instr.asm_mov.src);
      printf("\n");
      break;
    case ASM_UNARY:
      printf("Unary ");
      print_asm_un_op(instr->instr.asm_unary.op);
      printf(" ");
      print_operand(instr->instr.asm_unary.dst);
      printf(", ");
      print_operand(instr->instr.asm_unary.src);
      printf("\n");
      break;
    case ASM_BINARY:
      printf("Binary ");
      print_asm_alu_op(instr->instr.asm_binary.alu_op);
      printf(" ");
      print_operand(instr->instr.asm_binary.dst);
      printf(", ");
      print_operand(instr->instr.asm_binary.src1);
      printf(", ");
      print_operand(instr->instr.asm_binary.src2);
      printf("\n");
      break;
    case ASM_CMP:
      printf("Cmp ");
      print_operand(instr->instr.asm_cmp.src1);
      printf(", ");
      print_operand(instr->instr.asm_cmp.src2);
      printf("\n");
      break;
    case ASM_PUSH:
      printf("Push ");
      print_operand(instr->instr.asm_push.src);
      printf("\n");
      break;
    case ASM_CALL:
      printf("Call ");
      if (instr->instr.asm_call.label != NULL) {
        print_slice(instr->instr.asm_call.label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_INDIRECT_CALL:
      printf("IndirectCall ");
      if (instr->instr.asm_indirect_call.src != NULL){
        print_operand(instr->instr.asm_indirect_call.src);
      }
      printf("\n");
      break;
    case ASM_JUMP:
      printf("Jump ");
      if (instr->instr.asm_jump.label != NULL) {
        print_slice(instr->instr.asm_jump.label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_COND_JUMP:
      printf("CondJump ");
      print_asm_condition(instr->instr.asm_cond_jump.cond);
      printf(" ");
      if (instr->instr.asm_cond_jump.label != NULL) {
        print_slice(instr->instr.asm_cond_jump.label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_LABEL:
      printf("Label ");
      if (instr->instr.asm_label.label != NULL) {
        print_slice(instr->instr.asm_label.label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_RET:
      printf("Ret\n");
      break;
    case ASM_GET_ADDRESS:
      printf("GetAddress ");
      print_operand(instr->instr.asm_get_address.dst);
      printf(", &");
      print_operand(instr->instr.asm_get_address.src);
      printf("\n");
      break;
    case ASM_BOUNDARY:
      struct SourceLocation loc = source_location_from_ptr(instr->instr.asm_boundary.loc);
      const char* filename = source_filename_for_ptr(instr->instr.asm_boundary.loc);
      printf("Line %s:%zu:%zu\n", filename, loc.line, loc.column);
      break;
    case ASM_TRUNC:
      printf("Trunc ");
      print_operand(instr->instr.asm_trunc.dst);
      printf(", ");
      print_operand(instr->instr.asm_trunc.src);
      printf(", %zu", instr->instr.asm_trunc.size);
      printf("\n");
      break;
    case ASM_EXTEND:
      printf("Extend ");
      print_operand(instr->instr.asm_extend.dst);
      printf(", ");
      print_operand(instr->instr.asm_extend.src);
      printf(", %zu", instr->instr.asm_extend.size);
      printf("\n");
      break;
    case ASM_LOAD:
      printf("Load ");
      print_operand(instr->instr.asm_load.dst);
      printf(", ");
      print_operand(instr->instr.asm_load.src);
      printf("\n");
      break;
    case ASM_STORE:
      printf("Store ");;
      print_operand(instr->instr.asm_store.dst);
      printf(", ");
      print_operand(instr->instr.asm_store.src);
      printf("\n");
      break;
    default:
      printf("Instr?\n");
      break;
  }
}

// Print a linked list of ASM instructions.
static void print_asm_instrs(const struct AsmInstr* instrs, unsigned tabs) {
  for (const struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) {
    print_asm_instr(cur, tabs);
  }
}

// Print a top-level ASM node (function or static variable).
static void print_asm_top_level(const struct AsmTopLevel* top, unsigned tabs) {
  if (top == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (top->type) {
    case ASM_FUNC:
      printf("Func ");
      if (top->top.asm_func.name != NULL) {
        print_slice(top->top.asm_func.name);
      } else {
        printf("<null>");
      }
      printf(top->top.asm_func.global ? " global\n" : " local\n");

      print_tabs(tabs + 1);
      printf("Body:\n");
      print_asm_instrs(top->top.asm_func.body, tabs + 2);
      break;
    case ASM_STATIC_VAR:
      printf("StaticVar ");
      if (top->top.asm_static_var.name != NULL) {
        print_slice(top->top.asm_static_var.name);
      } else {
        printf("<null>");
      }
      printf(top->top.asm_static_var.global ? " global " : " local ");
      printf("align=%d ", top->top.asm_static_var.alignment);
      print_static_init(top->top.asm_static_var.init_values);
      printf("\n");
      break;
    case ASM_STATIC_CONST:
      printf("StaticConst ");
      if (top->top.asm_static_const.name != NULL) {
        print_slice(top->top.asm_static_const.name);
      } else {
        printf("<null>");
      }
      printf(" ");
      printf("align=%d ", top->top.asm_static_const.alignment);
      print_static_init(top->top.asm_static_const.init_values);
      printf("\n");
      break;
    case ASM_SECTION:
      printf("Section ");
      if (top->top.asm_section.name != NULL) {
        print_slice(top->top.asm_section.name);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_ALIGN:
      printf("Align %d\n", top->top.asm_align.alignment);
      break;
    default:
      printf("TopLevel?\n");
      break;
  }
}

// Print an entire ASM program for debugging.
void print_asm_prog(const struct AsmProg* prog) {
  if (prog == NULL) {
    printf("AsmProg <null>\n");
    return;
  }

  printf("AsmProg\n");
  for (const struct AsmTopLevel* cur = prog->head; cur != NULL; cur = cur->next) {
    print_asm_top_level(cur, 1);
  }
}
