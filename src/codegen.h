#ifndef CODEGEN_H
#define CODEGEN_H

/*
import AsmGen(getSrcs, getDst)
import TypedAST (StaticInit(getStaticInit, ZeroInit))

data MachineInstr = Add  Reg Reg Reg
                  | Addc Reg Reg Reg -- add w/ carry
                  | Sub  Reg Reg Reg
                  | Subc Reg Reg Reg -- subtract with carry
                  | And  Reg Reg Reg
                  | Or   Reg Reg Reg
                  | Xor  Reg Reg Reg
                  | Nand Reg Reg Reg
                  | Addi Reg Reg Int
                  | Sw   Reg Reg Int -- store word
                  | Lw   Reg Reg Int -- load word
                  | Not  Reg Reg
                  | Shl  Reg Reg -- shift left
                  | Shr  Reg Reg -- shift right
                  | Rotl Reg Reg -- rotate left
                  | Rotr Reg Reg -- rotate right
                  | Sshr Reg Reg -- signed shift right
                  | Shrc Reg Reg -- shift right w/ carry
                  | Shlc Reg Reg -- shift left w/ carry
                  | Cmp  Reg Reg -- compare
                  | Mov  Reg Reg
                  | Lui  Reg Int -- load upper immediate
                  | Movi Reg Imm
                  | Jalr Reg Reg -- jump and link register
                  | Push Reg
                  | Pop  Reg
                  | Call String
                  | Bz   Imm -- branch if zero
                  | Bp   Imm -- branch if positive
                  | Bn   Imm -- branch if negative
                  | Bc   Imm -- branch if carry
                  | Bo   Imm -- branch if overflow
                  | Bnz  Imm -- branch if nonzero
                  | Jmp  Imm -- unconditional jump
                  | Bnc  Imm -- branch if not carry
                  | Bg   Imm -- branch if greater (signed)
                  | Bge  Imm -- branch if greater or equal (signed)
                  | Bl   Imm -- branch if less (signed)
                  | Ble  Imm -- branch if less or equal (signed)
                  | Ba   Imm -- branch if above (unsigned)
                  | Bae  Imm -- branch if above or equal (unsigned)
                  | Bb   Imm -- branch if below (unsigned)
                  | Bbe  Imm -- branch if below or equal (unsigned)
                  | Clf -- clear flags
                  | Nop
                  | Sys Exception -- calls the OS
                  | Label String
                  | Fill Int -- inserts data
                  | Space Int -- inserts 0s
                  | NlComment String
                  | Comment String
                  deriving (Show)

data Imm = ImmLit Int | ImmLabel String

instance Show Imm where
  show (ImmLit n) = show n
  show (ImmLabel s) = s

-- will add more exceptions as OS is developed
data Exception = Exit
  deriving (Show)
*/

#endif // CODEGEN_H