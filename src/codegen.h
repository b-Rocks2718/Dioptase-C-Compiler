#ifndef CODEGEN_H
#define CODEGEN_H

#include "asm_gen.h"

// Classify target machine instruction variants.
enum MachineInstrType {
  // real instructions
  MACHINE_AND,
  MACHINE_NAND,
  MACHINE_OR,
  MACHINE_NOR,
  MACHINE_XOR,
  MACHINE_XNOR,
  MACHINE_NOT,
  MACHINE_LSL,
  MACHINE_LSR,
  MACHINE_ASR,
  MACHINE_ROTL,
  MACHINE_ROTR,
  MACHINE_LSLC,
  MACHINE_LSRC,
  MACHINE_ADD,
  MACHINE_ADDC,
  MACHINE_SUB,
  MACHINE_SUBB,
  MACHINE_EXTEND_B,
  MACHINE_EXTEND_D,
  MACHINE_TRUNCATE_B,
  MACHINE_TRUNCATE_D,
  MACHINE_LUI,
  MACHINE_SWA,
  MACHINE_LWA,
  MACHINE_LW,
  MACHINE_SW,
  MACHINE_SDA,
  MACHINE_LDA,
  MACHINE_LD,
  MACHINE_SD,
  MACHINE_SBA,
  MACHINE_LBA,
  MACHINE_LB,
  MACHINE_SB,
  MACHINE_BR,
  MACHINE_BZ,
  MACHINE_BNZ,
  MACHINE_BS,
  MACHINE_BNS,
  MACHINE_BC, 
  MACHINE_BNC,
  MACHINE_BO,
  MACHINE_BNO,
  MACHINE_BPS,
  MACHINE_BNPS,
  MACHINE_BG,
  MACHINE_BGE,
  MACHINE_BL,
  MACHINE_BLE,
  MACHINE_BA,
  MACHINE_BAE,
  MACHINE_BB,
  MACHINE_BBE,
  MACHINE_BRA,
  MACHINE_BZA,
  MACHINE_BNZA,
  MACHINE_BSA,
  MACHINE_BNSA,
  MACHINE_BCA, 
  MACHINE_BNCA,
  MACHINE_BOA,
  MACHINE_BNOA,
  MACHINE_BPSA,
  MACHINE_BNPSA,
  MACHINE_BGA,
  MACHINE_BGEA,
  MACHINE_BLA,
  MACHINE_BLEA,
  MACHINE_BAA,
  MACHINE_BAEA,
  MACHINE_BBA,
  MACHINE_BBEA,
  MACHINE_TNCB,
  MACHINE_TNCD,
  MACHINE_SXTB,
  MACHINE_SXTD,
  MACHINE_SYS,

  // macros
  MACHINE_NOP,
  MACHINE_PUSH,
  MACHINE_POP,
  MACHINE_PUSHD,
  MACHINE_POPD,
  MACHINE_PUSHB,
  MACHINE_POPB,
  MACHINE_MOV,
  MACHINE_MOVI,
  MACHINE_CALL,
  MACHINE_RET,
  MACHINE_JMP,
  MACHINE_CMP,

  // directives
  MACHINE_FILL,
  MACHINE_FILD,
  MACHINE_FILB,
  MACHINE_SPACE,
  MACHINE_GLOBAL,
  MACHINE_SECTION,
  MACHINE_ALIGN,

  // other
  MACHINE_COMMENT,
  MACHINE_NLCOMMENT,
  MACHINE_NEWLINE,
  MACHINE_LABEL,
  MACHINE_DEBUG_LOC,
  MACHINE_DEBUG_LOCAL,
};

// Own the linked list of generated machine instructions.
struct MachineProg {
  struct MachineInstr* head;
  struct MachineInstr* tail;
};

// Three-register ALU operation; imm == 0 selects the register form using rc.
struct MachineAlu {
  enum Reg ra;
  enum Reg rb;
  enum Reg rc;
  int imm;
};

// Two-register operation used by moves, shifts, compares, and branches.
struct MachineReg2 {
  enum Reg ra;
  enum Reg rb;
};

// Single-register operation used by push and pop.
struct MachineReg {
  enum Reg ra;
};

// Load or store with a base register, optional label, and displacement.
struct MachineMem {
  enum Reg ra;
  enum Reg rb;
  struct Slice* label;
  int imm;
};

// Register/immediate or register/label move (movi, lui).
struct MachineMovi {
  enum Reg ra;
  struct Slice* label;
  int imm;
};

// Label or immediate target used by calls, jumps, and directives.
struct MachineTarget {
  struct Slice* label;
  int imm;
};

// Assembler comment text.
struct MachineComment {
  struct Slice* text;
};

// Label definition.
struct MachineLabel {
  struct Slice* name;
};

// Source location for a debug line marker.
struct MachineDebugLoc {
  const char* loc;
};

// Stack-local debug entry: name, BP-relative offset, and size in bytes.
struct MachineDebugLocal {
  struct Slice* name;
  int offset;
  int size;
};

// Select the concrete machine-instruction payload identified by MachineInstrType.
union MachineInstrVariant {
  struct MachineAlu alu;
  struct MachineReg2 reg2;
  struct MachineReg reg;
  struct MachineMem mem;
  struct MachineMovi movi;
  struct MachineTarget target;
  struct MachineComment comment;
  struct MachineLabel label;
  struct MachineDebugLoc debug_loc;
  struct MachineDebugLocal debug_local;
};

// Link one machine instruction with its kind and list pointer.
struct MachineInstr {
  enum MachineInstrType type;
  union MachineInstrVariant instr;
  struct MachineInstr* next;
};

struct MachineProg* prog_to_machine(struct AsmProg* asm_prog);

struct MachineProg* top_level_to_machine(struct AsmTopLevel* asm_top);

struct MachineProg* instr_to_machine(struct Slice* name, struct AsmInstr* instr);

#endif // CODEGEN_H
