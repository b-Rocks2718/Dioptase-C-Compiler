#include <inttypes.h>
#include <stdio.h>

#include "TAC.h"
#include "slice.h"

// Purpose: Emit indentation for TAC output formatting.
// Inputs: tabs is the indentation level in 4-space units.
// Outputs: Writes spaces to stdout.
// Invariants/Assumptions: tabs is small enough to avoid excessive output.
static void print_tabs(unsigned tabs) {
  for (unsigned i = 0; i < tabs; ++i) {
    printf("    ");
  }
}

// Purpose: Print a TAC value (constant or variable).
// Inputs: val may be NULL; otherwise points to a TAC value.
// Outputs: Writes a compact value representation to stdout.
// Invariants/Assumptions: Constants are stored as 64-bit raw values.
static void print_tac_val(const struct Val* val) {
  if (val == NULL) {
    printf("<null>");
    return;
  }

  switch (val->val_type) {
    case CONSTANT:
      if (val->type != NULL && is_signed_type(val->type)) {
        printf("Const(%" PRId64 ")", (int64_t)val->val.const_value);
      } else {
        printf("Const(%" PRIu64 ")", (uint64_t)val->val.const_value);
      }
      break;
    case VARIABLE:
      printf("Var(");
      print_slice(val->val.var_name);
      printf(")");
      break;
    default:
      printf("Val(?)");
      break;
  }
}

// Purpose: Print a TAC condition mnemonic.
// Inputs: cond is the TAC condition enum to print.
// Outputs: Writes the mnemonic to stdout.
// Invariants/Assumptions: cond is a valid TACCondition.
static void print_tac_condition(enum TACCondition cond) {
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

// Purpose: Print a binary operator mnemonic used in TAC.
// Inputs: op is the AST binary operator enum.
// Outputs: Writes the operator mnemonic to stdout.
// Invariants/Assumptions: op is a valid BinOp.
static void print_tac_bin_op(enum ALUOp op) {
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
    case ALU_LSL:
      printf("LslOp");
      break;
    case ALU_LSR:
      printf("LsrOp");
      break;
    case ALU_ASL:
      printf("AslOp");
      break;
    case ALU_ASR:
      printf("AsrOp");
      break;
    default:
      printf("ALUOp?");
      break;
  }
}

// Purpose: Print a unary operator mnemonic used in TAC.
// Inputs: op is the AST unary operator enum.
// Outputs: Writes the operator mnemonic to stdout.
// Invariants/Assumptions: op is a valid UnOp.
static void print_tac_un_op(enum UnOp op) {
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

// Purpose: Print a static initializer record for TAC output.
// Inputs: init may be NULL; otherwise points to a static initializer descriptor.
// Outputs: Writes initializer details to stdout.
// Invariants/Assumptions: Only scalar initializers are represented.
static void print_tac_init(const struct IdentInit* init) {
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
      struct InitList* cur = init->init_list;
      bool first = true;
      while (cur != NULL) {
        if (!first) {
          printf(", ");
        }
        if (cur->value.int_type == INT_INIT || cur->value.int_type == LONG_INIT) {
          printf("%" PRId64, (int64_t)cur->value.value);
        } else {
          printf("%" PRIu64, (uint64_t)cur->value.value);
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

// Purpose: Print a single TAC instruction at a given indentation level.
// Inputs: instr points to the instruction; tabs is the indentation level.
// Outputs: Writes one formatted instruction line to stdout.
// Invariants/Assumptions: instr is non-NULL and variants are populated.
static void print_tac_instr(const struct TACInstr* instr, unsigned tabs) {
  if (instr == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (instr->type) {
    case TACRETURN:
      printf("Return ");
      print_tac_val(instr->instr.tac_return.dst);
      printf("\n");
      break;
    case TACUNARY:
      printf("Unary ");
      print_tac_un_op(instr->instr.tac_unary.op);
      printf(" ");
      print_tac_val(instr->instr.tac_unary.dst);
      printf(", ");
      print_tac_val(instr->instr.tac_unary.src);
      printf("\n");
      break;
    case TACBINARY:
      printf("Binary ");
      print_tac_bin_op(instr->instr.tac_binary.alu_op);
      printf(" ");
      print_tac_val(instr->instr.tac_binary.dst);
      printf(", ");
      print_tac_val(instr->instr.tac_binary.src1);
      printf(", ");
      print_tac_val(instr->instr.tac_binary.src2);
      if (instr->instr.tac_binary.type != NULL) {
        printf(" : ");
        print_type(instr->instr.tac_binary.type);
      }
      printf("\n");
      break;
    case TACCOND_JUMP:
      printf("CondJump ");
      print_tac_condition(instr->instr.tac_cond_jump.condition);
      printf(" ");
      print_slice(instr->instr.tac_cond_jump.label);
      printf("\n");
      break;
    case TACCMP:
      printf("Cmp ");
      print_tac_val(instr->instr.tac_cmp.src1);
      printf(", ");
      print_tac_val(instr->instr.tac_cmp.src2);
      printf("\n");
      break;
    case TACJUMP:
      printf("Jump ");
      print_slice(instr->instr.tac_jump.label);
      printf("\n");
      break;
    case TACLABEL:
      printf("Label ");
      print_slice(instr->instr.tac_label.label);
      printf("\n");
      break;
    case TACCOPY:
      printf("Copy ");
      print_tac_val(instr->instr.tac_copy.dst);
      printf(", ");
      print_tac_val(instr->instr.tac_copy.src);
      printf("\n");
      break;
    case TACCALL: {
      printf("Call ");
      print_slice(instr->instr.tac_call.func_name);
      printf(" -> ");
      print_tac_val(instr->instr.tac_call.dst);
      printf(" (");
      for (size_t i = 0; i < instr->instr.tac_call.num_args; i++) {
        if (i != 0) {
          printf(", ");
        }
        print_tac_val(&instr->instr.tac_call.args[i]);
      }
      printf(")\n");
      break;
    }
    case TACGET_ADDRESS:
      printf("GetAddress ");
      print_tac_val(instr->instr.tac_get_address.dst);
      printf(", &");
      print_tac_val(instr->instr.tac_get_address.src);
      printf("\n");
      break;
    case TACLOAD:
      printf("Load ");
      print_tac_val(instr->instr.tac_load.dst);
      printf(", [");
      print_tac_val(instr->instr.tac_load.src_ptr);
      printf("]\n");
      break;
    case TACSTORE:
      printf("Store [");
      print_tac_val(instr->instr.tac_store.dst_ptr);
      printf("], ");
      print_tac_val(instr->instr.tac_store.src);
      printf("\n");
      break;
    case TACCOPY_TO_OFFSET:
      printf("CopyToOffset ");
      print_tac_val(instr->instr.tac_copy_to_offset.dst);
      printf(", ");
      print_tac_val(instr->instr.tac_copy_to_offset.src);
      printf(", %d\n", instr->instr.tac_copy_to_offset.offset);
      break;
    default:
      printf("Instr?\n");
      break;
  }
}

// Purpose: Print a linked list of TAC instructions.
// Inputs: instrs is the head of the TAC list; tabs is the indentation level.
// Outputs: Writes all instructions to stdout in order.
// Invariants/Assumptions: List links are well-formed (acyclic).
static void print_tac_instrs(const struct TACInstr* instrs, unsigned tabs) {
  for (const struct TACInstr* cur = instrs; cur != NULL; cur = cur->next) {
    print_tac_instr(cur, tabs);
  }
}

// Purpose: Print a top-level TAC node (function or static variable).
// Inputs: top points to the TopLevel node; tabs is the indentation level.
// Outputs: Writes the top-level representation to stdout.
// Invariants/Assumptions: top points to a valid TopLevel node.
static void print_tac_top_level(const struct TopLevel* top, unsigned tabs) {
  if (top == NULL) {
    return;
  }

  print_tabs(tabs);
  switch (top->type) {
    case FUNC: {
      printf("Func ");
      print_slice(top->name);
      printf(top->global ? " global\n" : " local\n");

      print_tabs(tabs + 1);
      printf("Params: ");
      if (top->num_params == 0) {
        printf("<none>");
      } else {
        for (size_t i = 0; i < top->num_params; i++) {
          if (i != 0) {
            printf(", ");
          }
          print_slice(top->params[i]);
        }
      }
      printf("\n");

      print_tabs(tabs + 1);
      printf("Body:\n");
      print_tac_instrs(top->body, tabs + 2);
      break;
    }
    case STATIC_VAR:
      printf("StaticVar ");
      print_slice(top->name);
      printf(top->global ? " global " : " local ");
      printf("type=");
      if (top->var_type != NULL) {
        print_type(top->var_type);
      } else {
        printf("<null>");
      }
      printf(" ");
      print_tac_init(top->init_values);
      printf("\n");
      break;
    default:
      printf("TopLevel?\n");
      break;
  }
}

// Purpose: Print an entire TAC program for debugging.
// Inputs: prog points to the TAC program to print.
// Outputs: Writes the TAC program to stdout.
// Invariants/Assumptions: Program top-level list is well-formed.
void print_tac_prog(struct TACProg* prog) {
  if (prog == NULL) {
    printf("TACProg <null>\n");
    return;
  }

  printf("TACProg\n");
  for (struct TopLevel* cur = prog->statics; cur != NULL; cur = cur->next) {
    print_tac_top_level(cur, 1);
  }
  for (struct TopLevel* cur = prog->head; cur != NULL; cur = cur->next) {
    print_tac_top_level(cur, 1);
  }
}
