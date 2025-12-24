# Dioptase C Compiler

I'm in the process of converting a [partial C compiler](https://github.com/b-Rocks2718/c-compiler) I wrote in Haskell into C (I want it to eventually compile itself). 

## Tests

Golden tests live in `tests/preprocess`, `tests/lexer`, and `tests/lexer_invalid`. Each case is a `.c` file with a matching `.ok` file for expected output. Running `make test` writes `test.out` files next to each case and diffs them against the `.ok` files.

- Preprocessor cases: `.ok` contains the exact output of `-preprocess`.
- Lexer cases: `.ok` matches the exact output of `print_token_array()` as printed by `-tokens`.
- Invalid lexer cases: `-tokens` should fail (non-zero exit). If a `.ok` file exists, it is compared against the error output.

```sh
make -C Dioptase-Languages/Dioptase-C-Compiler
./Dioptase-Languages/Dioptase-C-Compiler/build/bcc -tokens tests/lexer/example.c > tests/lexer/example.ok
./Dioptase-Languages/Dioptase-C-Compiler/build/bcc -preprocess tests/preprocess/example.c > tests/preprocess/example.ok
```

Run all tests with:

```sh
make -C Dioptase-Languages/Dioptase-C-Compiler test
```
