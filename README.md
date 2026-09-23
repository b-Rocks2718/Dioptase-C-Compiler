# Dioptase C Compiler

[![CI](https://github.com/b-Rocks2718/Dioptase-C-Compiler/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/b-Rocks2718/Dioptase-C-Compiler/actions/workflows/ci.yml)

My implementation of of the compiler described in [Writing a C Compiler](https://nostarch.com/writing-c-compiler) by Nora Sandler. Targets the [Dioptase architecture](https://github.com/b-Rocks2718/Dioptase) and relies on the [Dioptase assembler](). The generated machine code can be run with the [Dioptase emulator]().

## Usage

Build with:

```sh
make all
```

This produces `build/debug/bcc`. You can also build an optimized binary with `make release` (writes `build/release/bcc`).

Run the compiler with:

```sh
./build/debug/bcc -flags path_to_file.c
```

Accepted flags:

```text
-preprocess           print preprocessed output
-tokens               print the token stream
-ast                  print the parsed AST
-idents               print the AST after identifier resolution
-labels               print the AST after loop/switch/goto labeling
-types                print the symbol table and AST after typechecking
-tac                  print the generated TAC
-cfg                  print the control-flow graph of each function
-asm                  print the intermediate assembly IR (before machine code generation)
-interp               run the TAC interpreter and print the result
-s                    emit assembly instead of assembling to hex
-bin                  pass -bin to the assembler to emit a raw binary output
-kernel               pass -kernel to the assembler
-crt <dir>            use CRT sources from <dir> for user-mode links
-g                    include debug info
-o <file>             set the output file path (defaults to a.hex, a.bin with -bin, or a.s with -s)
-DNAME[=value]        define a preprocessor macro (repeatable)
```

Flags can be combined to dump multiple stages. `-preprocess` exits early unless `-tokens` or `-ast` is also specified.

### Optimization flags

Optimizations are disabled by default. The following passes are currently implemented and can be enabled independently:

```text
-constant-fold        fold constant TAC expressions and branches
-dead-code            remove unreachable TAC blocks and redundant control flow
-copy-prop            copy propagation
-dead-store           dead-store elimination
-tail-call            tail-call optimization
```

`-opt` enables every optimization switch. At present, its effective optimizations are the five passes above; it also enables the reserved switches below so that newly implemented passes will automatically become part of `-opt`.

These optimization switches are accepted by the compiler, but their passes are not implemented yet and they currently have no effect:

```text
-inline               function inlining
-peephole             low-level peephole optimization
-reg-alloc            register allocation
```

Optimization flags may be combined. For example, `bcc -constant-fold -dead-code input.c` runs both of those passes. The `make release` target only optimizes the `bcc` executable itself; it does not enable optimization of compiled C programs.

For user-mode links, `bcc` always passes `-crt <dir>` through to the assembler. Without an explicit override, it prefers the compiler-local CRT under `Dioptase-Languages/Dioptase-C-Compiler/crt` when `DIOPTASE_ROOT` is set.

## Supported Features

### Preprocessor

- `#define` (object-like macros. no function-like macros yet)
- `#include` (only supports `""` includes for now)
- `#ifdef`, `#ifndef`, `#else`, `#endif`
- `__FILE__`, `__LINE__` builtin macros
- comments (`//` and `/* ... */`)

### C Subset

Supported:

- Types: `signed`/`unsigned` `int` and `short`, `void`, `char` types, arrays, functions, strings, structs, unions, enums, and pointers to these types
- Type qualifiers: `const` and `volatile` on objects, pointers, aggregates, and parameters
- Storage classes: `static`, `extern`
- Declarations: global and local variables, function declarations/definitions
- Expressions:
  - literals: decimal and `0x` hex integers
  - variables, assignments (`=`, compound assignments)
  - unary: `-`, `~`, `!`, `&`, `*`, `+`, pre/post `++` and `--`
  - binary: `+`, `-`, `*`, `/`, `%`, shifts, bitwise ops, boolean ops, comparisons, and member access operators  
  - ternary `?:`
  - function calls
  - `sizeof` types and expressions
  - statement expressions
- Statements: expression statements, `return`, blocks, `if`/`else`, `while`, `do`/`while`,
  `for`, `break`, `continue`, `switch`/`case`/`default`, labels and `goto`
- Attributes: just `cleanup` for now

Limitations:
- No floating-point
- No `long` or `long long` integers
- No `typedef`, `inline`, or `restrict`
- No variadic functions
- No inline assembly

## Tests

Stage-specific tests live in these folders:

- `tests/preprocess` and `tests/preprocess_invalid` (`-preprocess`, optional `.flags` file for extra args)
- `tests/lexer` and `tests/lexer_invalid` (`-tokens`)
- `tests/parser` and `tests/parser_invalid` (`-ast`)
- `tests/idents` and `tests/idents_invalid` (`-idents`)
- `tests/labels` and `tests/labels_invalid` (`-labels`)
- `tests/types` and `tests/types_invalid` (`-types`)
- `tests/cfg` (`-cfg`)
- `tests/opt` (`-tac -cfg`, with optimization flags from a matching `.flags` file)

Each test case is a `.c` file with a matching `.ok` file for expected output. `make test` (debug build) and `make test-release` (release build) write per-test `.out` files next to each case and diff them against the `.ok` files. Invalid tests must fail with a non-zero exit; if a `.ok` file exists, its contents are compared against the captured output.

Run the local test suites with:

```sh
make test
make test-release
```

`make test` and `make test-release` also build and run the TAC interpreter, TAC execution, emulator execution, and full emulator execution tests from `tests/tac_interpreter_tests.c`, `tests/tac_exec_tests.c`, `tests/emu_exec_tests.c`, and `tests/emu_exec_full_tests.c` All three execution suites run every `.c` file in `tests/exec/`, compile it with the host C compiler as ground truth, and compare `main`'s return value. Each execution suite runs once without optimizations and once with `OPT_TEST_FLAGS`, which defaults to `-opt` near the top of the Makefile. A fixture containing the text `tac-exec: skip` (conventionally in its header comment, with the reason) is skipped by the TAC execution suite but still runs in both emulator suites; use this for programs the TAC interpreter cannot run, such as ones that pass structs by value.

I also use [test cases from Writing a C Compiler](https://github.com/nlsandler/writing-a-c-compiler-tests#) via the wrapper in `tests/wacc_tac_compiler.py`. Run them with:

```sh
make test-wacc
make test-wacc-opt
make test-wacc-release
make test-wacc-release-opt
make test-wacc-kernel
make test-wacc-kernel-opt
make test-wacc-kernel-release
make test-wacc-kernel-release-opt
make test-tac-wacc
make test-tac-wacc-opt
make test-tac-wacc-release
make test-tac-wacc-release-opt
```

`test-wacc*` runs the WACC tests via the emulator + assembler pipeline (simple emulator), `test-wacc-kernel*` runs them via the full emulator using kernel-mode assembly plus `tests/kernel/init.s` and `tests/kernel/arithmetic.s`, and `test-tac-wacc*` uses the TAC interpreter wrapper. Targets ending in `-opt` are separate slower runs that also pass `OPT_TEST_FLAGS` to the compiler. For example, `make test-wacc-opt OPT_TEST_FLAGS=-constant-fold` isolates constant folding in WACC tests, while `make test OPT_TEST_FLAGS=-constant-fold` does the same for the normal execution suites. The existing WACC targets continue to test without compiler optimizations. The WACC runner defaults can be overridden with `WACC_CORE_CHAPTER`, `WACC_EXTRA_CHAPTERS`, `WACC_EXTRA_CREDIT`, `WACC_SKIP_TYPES`, and `WACC_ARGS`. Kernel runs can also override `DIOPTASE_EMULATOR_FULL`, `DIOPTASE_WACC_KERNEL_INIT`, and `DIOPTASE_WACC_KERNEL_ARITH`.

## Makefile Targets

Build and clean:

- `make all` (same as `make debug`)
- `make debug`
- `make release`
- `make clean`
- `make purge`

Test suites:

- `make test`
- `make test-release`
- `make test-wacc`
- `make test-wacc-opt`
- `make test-wacc-release`
- `make test-wacc-release-opt`
- `make test-wacc-kernel`
- `make test-wacc-kernel-opt`
- `make test-wacc-kernel-release`
- `make test-wacc-kernel-release-opt`
- `make test-tac-wacc`
- `make test-tac-wacc-opt`
- `make test-tac-wacc-release`
- `make test-tac-wacc-release-opt`

Tooling helpers:

- `make emulator-debug`
- `make emulator-release`
- `make assembler-debug`
- `make assembler-release`
