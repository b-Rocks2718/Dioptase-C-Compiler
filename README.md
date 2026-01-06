# Dioptase C Compiler

My implementation of of the compiler described in [Writing a C Compiler](https://nostarch.com/writing-c-compiler) by Nora Sandler.

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
-interp               run the TAC interpreter and print the result
-DNAME[=value]        define a preprocessor macro (repeatable)
```

Flags can be combined to dump multiple stages. `-preprocess` exits early unless `-tokens` or `-ast` is also specified.

## Supported Features

### Preprocessor

- `#define` (object-like macros. no function-like macros yet)
- `#include` (only supports `""` includes for now)
- `#ifdef`, `#ifndef`, `#else`, `#endif`
- `__FILE__`, `__LINE__` builtin macros
- comments (`//` and `/* ... */`)

### C Subset

Supported:

- Types: `int`, `unsigned int`, and pointers to these types
- Storage classes: `static`, `extern`
- Declarations: global and local variables, function declarations/definitions
- Expressions:
  - literals: decimal and `0x` hex integers
  - variables, assignments (`=`, compound assignments)
  - unary: `-`, `~`, `!`, `&`, `*`, pre/post `++` and `--`
  - binary: `+`, `-`, `*`, `/`, `%`, shifts, bitwise ops, comparisons, `&&`, `||`
  - ternary `?:`
  - function calls
- Statements: expression statements, `return`, blocks, `if`/`else`, `while`, `do`/`while`,
  `for`, `break`, `continue`, `switch`/`case`/`default`, labels and `goto`

Known limitations:

- No arrays, structs/unions, enums, `sizeof`, floating-point, `char`, or string literals
- No `short` or `long` integers, no `void` or `void*`
- No `typedef`, `const`, or `volatile`
- No multiple declarators per declaration (e.g., `int a, b;`)
- Global initializers must be integer literals (pointer globals may only use null pointer literals)

## Tests

Stage-specific tests live in these folders:

- `tests/preprocess` and `tests/preprocess_invalid` (`-preprocess`, optional `.flags` file for extra args)
- `tests/lexer` and `tests/lexer_invalid` (`-tokens`)
- `tests/parser` and `tests/parser_invalid` (`-ast`)
- `tests/idents` and `tests/idents_invalid` (`-idents`)
- `tests/labels` and `tests/labels_invalid` (`-labels`)
- `tests/types` and `tests/types_invalid` (`-types`)

Each test case is a `.c` file with a matching `.ok` file for expected output. `make test` (debug build) and `make test-release` (release build) write per-test `.out` files next to each case and diff them against the `.ok` files. Invalid tests must fail with a non-zero exit; if a `.ok` file exists, its contents are compared against the captured output.

Run the local test suites with:

```sh
make test
make test-release
```

`make test` and `make test-release` also build and run the TAC interpreter and TAC execution tests from `tests/tac_interpreter_tests.c` and `tests/tac_exec_tests.c` (which uses sources in `tests/tac_exec/`).

I also use [test cases from Writing a C Compiler](https://github.com/nlsandler/writing-a-c-compiler-tests#) via the wrapper in `tests/wacc_tac_compiler.py`. Run them with:

```sh
make test-wacc
```

The WACC runner defaults can be overridden with `WACC_CORE_CHAPTER`, `WACC_EXTRA_CHAPTERS`, `WACC_EXTRA_CREDIT`, `WACC_SKIP_TYPES`, and `WACC_ARGS`.
