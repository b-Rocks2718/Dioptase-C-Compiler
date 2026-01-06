#include <inttypes.h>
#include <stdio.h>

#include "asm_gen.h"
#include "slice.h"
#include "typechecking.h"

// Purpose: Provide human-readable printing of ASM IR for debugging.
// Inputs/Outputs: Functions emit text to stdout.
// Invariants/Assumptions: The ASM IR lists are well-formed and acyclic.

// Purpose: Emit indentation for ASM output formatting.
// Inputs: tabs is the indentation level in 4-space units.
// Outputs: Writes spaces to stdout.
// Invariants/Assumptions: tabs is small enough to avoid excessive output.
static void print_tabs(unsigned tabs) {
  static const char kIndent[] = "    ";
  for (unsigned i = 0; i < tabs; ++i) {
    fputs(kIndent, stdout);
  }
}

// Purpose: Convert a register enum to its canonical name.
// Inputs: reg is the register identifier to name.
// Outputs: Returns a string literal describing the register.
// Invariants/Assumptions: Unknown registers are reported as "R?".
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

// Purpose: Print a register with its architectural alias (if any).
// Inputs: reg is the register to print.
// Outputs: Writes a register name to stdout.
// Invariants/Assumptions: BP/SP/RA match the constants in asm_gen.h.
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

// Purpose: Print an ASM operand in a compact readable form.
// Inputs: opr is the operand to print (may be NULL).
// Outputs: Writes the operand representation to stdout.
// Invariants/Assumptions: Operand fields match the operand type.
static void print_operand(const struct Operand* opr) {
  if (opr == NULL) {
    printf("<null>");
    return;
  }

  switch (opr->type) {
    case OPERAND_LIT:
      printf("Lit(%d)", opr->lit_value);
      break;
    case OPERAND_REG:
      printf("Reg(");
      print_reg(opr->reg);
      printf(")");
      break;
    case OPERAND_PSEUDO:
      printf("Pseudo(");
      print_slice(opr->pseudo);
      printf(")");
      break;
    case OPERAND_PSEUDO_MEM:
      printf("PseudoMem(");
      print_slice(opr->pseudo);
      printf(", %d)", opr->lit_value);
      break;
    case OPERAND_MEMORY:
      printf("Mem(");
      print_reg(opr->reg);
      printf(", %d)", opr->lit_value);
      break;
    case OPERAND_DATA:
      printf("Data(");
      print_slice(opr->pseudo);
      printf(")");
      break;
    default:
      printf("Operand(?)");
      break;
  }
}

// Purpose: Print a TAC condition mnemonic used by ASM conditional jumps.
// Inputs: cond is the TAC condition enum to print.
// Outputs: Writes the mnemonic to stdout.
// Invariants/Assumptions: cond is a valid TACCondition.
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

// Purpose: Print a unary operator mnemonic used in ASM IR.
// Inputs: op is the unary operator enum.
// Outputs: Writes the operator mnemonic to stdout.
// Invariants/Assumptions: op is a valid UnOp.
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
    default:
      printf("UnOp?");
      break;
  }
}

// Purpose: Print a binary ALU operator mnemonic used in ASM IR.
// Inputs: op is the ALU operator enum.
// Outputs: Writes the operator mnemonic to stdout.
// Invariants/Assumptions: op is a valid ALUOp.
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
    case ALU_SHL:
      printf("ShlOp");
      break;
    case ALU_SHR:
      printf("ShrOp");
      break;
    case ALU_ASR:
      printf("AsrOp");
      break;
    default:
      printf("ALUOp?");
      break;
  }
}

// Purpose: Print a static initializer record for ASM output.
// Inputs: init may be NULL; otherwise points to a static initializer descriptor.
// Outputs: Writes initializer details to stdout.
// Invariants/Assumptions: Only scalar initializers are represented.
static void print_asm_init(const struct IdentInit* init) {
  if (init == NULL) {
    printf("init=<null>");
    return;
  }

  switch (init->init_type) {
    case NO_INIT:
      printf("init=none");
      break;
    case TENTATIVE:
      printf("init=tentative");
      break;
    case INITIAL: {
      printf("init=[");
      const struct InitList* cur = init->init_list;
      bool first = true;
      while (cur != NULL) {
        if (!first) {
          printf(", ");
        }
        switch (cur->value.int_type) {
          case INT_INIT:
          case LONG_INIT:
            printf("%" PRId64, (int64_t)cur->value.value);
            break;
          case UINT_INIT:
          case ULONG_INIT:
            printf("%" PRIu64, (uint64_t)cur->value.value);
            break;
          case ZERO_INIT:
            printf("0");
            break;
          default:
            printf("?");
            break;
        }
        first = false;
        cur = cur->next;
      }
      printf("]");
      break;
    }
    default:
      printf("init=?");
      break;
  }
}

// Purpose: Print a single ASM instruction at a given indentation level.
// Inputs: instr points to the instruction; tabs is the indentation level.
// Outputs: Writes one formatted instruction line to stdout.
// Invariants/Assumptions: instr is non-NULL and variants are populated.
static void print_asm_instr(const struct AsmInstr* instr, unsigned tabs) {
  if (instr == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (instr->type) {
    case ASM_MOV:
      printf("Mov ");
      print_operand(instr->dst);
      printf(", ");
      print_operand(instr->src1);
      printf("\n");
      break;
    case ASM_MOVSX:
      printf("Movsx ");
      print_operand(instr->dst);
      printf(", ");
      print_operand(instr->src1);
      printf("\n");
      break;
    case ASM_UNARY:
      printf("Unary ");
      print_asm_un_op(instr->unary_op);
      printf(" ");
      print_operand(instr->dst);
      printf(", ");
      print_operand(instr->src1);
      printf("\n");
      break;
    case ASM_BINARY:
      printf("Binary ");
      print_asm_alu_op(instr->alu_op);
      printf(" ");
      print_operand(instr->dst);
      printf(", ");
      print_operand(instr->src1);
      printf(", ");
      print_operand(instr->src2);
      printf("\n");
      break;
    case ASM_CMP:
      printf("Cmp ");
      print_operand(instr->src1);
      printf(", ");
      print_operand(instr->src2);
      printf("\n");
      break;
    case ASM_PUSH:
      printf("Push ");
      print_operand(instr->src1);
      printf("\n");
      break;
    case ASM_CALL:
      printf("Call ");
      if (instr->label != NULL) {
        print_slice(instr->label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_JUMP:
      printf("Jump ");
      if (instr->label != NULL) {
        print_slice(instr->label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_COND_JUMP:
      printf("CondJump ");
      print_asm_condition(instr->cond);
      printf(" ");
      if (instr->label != NULL) {
        print_slice(instr->label);
      } else {
        printf("<null>");
      }
      printf("\n");
      break;
    case ASM_LABEL:
      printf("Label ");
      if (instr->label != NULL) {
        print_slice(instr->label);
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
      print_operand(instr->dst);
      printf(", &");
      print_operand(instr->src1);
      printf("\n");
      break;
    default:
      printf("Instr?\n");
      break;
  }
}

// Purpose: Print a linked list of ASM instructions.
// Inputs: instrs is the head of the ASM list; tabs is the indentation level.
// Outputs: Writes all instructions to stdout in order.
// Invariants/Assumptions: List links are well-formed (acyclic).
static void print_asm_instrs(const struct AsmInstr* instrs, unsigned tabs) {
  for (const struct AsmInstr* cur = instrs; cur != NULL; cur = cur->next) {
    print_asm_instr(cur, tabs);
  }
}

// Purpose: Print a top-level ASM node (function or static variable).
// Inputs: top points to the AsmTopLevel node; tabs is the indentation level.
// Outputs: Writes the top-level representation to stdout.
// Invariants/Assumptions: top points to a valid AsmTopLevel node.
static void print_asm_top_level(const struct AsmTopLevel* top, unsigned tabs) {
  if (top == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (top->type) {
    case ASM_FUNC:
      printf("Func ");
      if (top->name != NULL) {
        print_slice(top->name);
      } else {
        printf("<null>");
      }
      printf(top->global ? " global\n" : " local\n");

      print_tabs(tabs + 1);
      printf("Body:\n");
      print_asm_instrs(top->body, tabs + 2);
      break;
    case ASM_STATIC_VAR:
      printf("StaticVar ");
      if (top->name != NULL) {
        print_slice(top->name);
      } else {
        printf("<null>");
      }
      printf(top->global ? " global " : " local ");
      printf("align=%d ", top->alignment);
      printf("inits=%zu ", top->num_inits);
      print_asm_init(top->init_values);
      printf("\n");
      break;
    default:
      printf("TopLevel?\n");
      break;
  }
}

// Purpose: Print an entire ASM program for debugging.
// Inputs: prog points to the ASM program to print.
// Outputs: Writes the ASM program to stdout.
// Invariants/Assumptions: Program top-level list is well-formed.
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
