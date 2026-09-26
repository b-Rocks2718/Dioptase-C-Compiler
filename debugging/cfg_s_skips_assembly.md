# `-cfg -s` skips assembly output

## Reported command

```sh
./build/debug/bcc tests/test.c -opt -cfg -s
```

## Baseline reproduction

- Exit status: `0`
- Standard output: the optimized CFG
- Expected output file: `a.s`
- Observed output file: absent
- Pre-change test suite: `261 / 261 tests passed`

## Cause

`-cfg` sets `any_stage_flag`, which makes `run_full` false. The
`stop_after_tac` condition then returns immediately after printing the CFG,
without considering that `-s` independently requests an assembly file.

Even if that return were bypassed in isolation, the assembly-IR stage was
guarded by `run_full || print_asm`, so a `-s` request combined with an earlier
diagnostic such as `-tokens` would reach machine lowering without constructing
an `AsmProg`.

## Fix

Treat `-s` as an explicit request to complete the compilation pipeline:

- `run_full` is true when no diagnostic stage is requested **or** when `-s`
  requests an assembly file.
- Early diagnostic-stage returns apply only when `run_full` is false.
- The assembly and machine stages follow `run_full`.

## Verification

- Exact-command reproduction: CFG printed and a non-empty `a.s` written.
