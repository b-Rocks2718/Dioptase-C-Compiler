# Dioptase C Compiler

I'm in the process of converting a [partial C compiler](https://github.com/b-Rocks2718/c-compiler) I wrote in Haskell into C (I want it to eventually compile itself). 

## Usage

Build with:

```sh
make all
```

Run the compiler with:

```sh
./build/bcc -preprocess tests/lexer/example.c
./build/bcc -tokens tests/preprocess/example.c
```

The first command runs the preprocessor on `example.c` and outputs the result to stdout. The second command lexes the preprocessed output and prints the tokens. I'm in the process of implementing the parser.

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

Tests live in `tests/preprocess`, `tests/lexer`, and `tests/lexer_invalid`. Each case is a `.c` file with a matching `.ok` file for expected output. Running `make test` writes `test.out` files next to each case and diffs them against the `.ok` files.

- Preprocessor cases: `.ok` contains the exact output of `-preprocess`.
- Lexer cases: `.ok` matches the exact output of `print_token_array()` as printed by `-tokens`.
- Invalid lexer cases: `-tokens` should fail (non-zero exit). If a `.ok` file exists, it is compared against the error output.

Run all tests with:

```sh
make test
```
