#ifndef MACHINE_PRINT_H
#define MACHINE_PRINT_H

#include <stdbool.h>

#include "codegen.h"

// Write a machine program as assembly text to a file.
// Returns true on success; false if file IO fails.
bool write_machine_prog_to_file(const struct MachineProg* prog, const char* path);

#endif // MACHINE_PRINT_H
