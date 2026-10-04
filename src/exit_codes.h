#ifndef EXIT_CODES_H
#define EXIT_CODES_H

// Process exit statuses for bcc. These are an external interface: the WACC
// Makefile targets pass the front-end codes (BCC_EXIT_INPUT..BCC_EXIT_TYPES)
// as --expected-error-codes, so an invalid program only counts as rejected
// when a front-end stage diagnosed it. Keep that Makefile list in sync when
// adding or renumbering codes.
enum BccExitCode {
  BCC_EXIT_OK = 0,
  // Bad command line, unreadable input file, or a preprocessing/lexing error.
  BCC_EXIT_INPUT = 1,
  BCC_EXIT_PARSE = 2,
  BCC_EXIT_RESOLVE = 3,
  BCC_EXIT_LABELS = 4,
  BCC_EXIT_TYPES = 5,
  // Writing the assembly file or running the assembler/linker failed. This
  // includes link errors in the user's program, such as a missing main.
  BCC_EXIT_OUTPUT = 6,
  // Internal compiler error: a pass after the front end hit a broken invariant
  // or an unsupported construct, or the compiler ran out of memory. Never a
  // valid way to reject a source program.
  BCC_EXIT_INTERNAL = 7,
};

#endif // EXIT_CODES_H
