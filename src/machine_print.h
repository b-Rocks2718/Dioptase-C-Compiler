#ifndef MACHINE_PRINT_H
#define MACHINE_PRINT_H

#include <stdbool.h>

#include "codegen.h"

// Purpose: Write a machine program as assembly text to a file.
// Inputs: prog is the machine program to emit; path is the output file name.
// Outputs: Returns true on success; false if file IO fails.
// Invariants/Assumptions: prog lists are well-formed and acyclic.
bool write_machine_prog_to_file(const struct MachineProg* prog, const char* path);

#endif // MACHINE_PRINT_H
