# WACC exclusion and diagnostic audit

## Scope

- Fix the WACC emulator compile-failure report, which previously printed only stderr even though `bcc` reports parse/TAC failures on stdout.
- Inventory WACC tests excluded by ignored-test paths, type filters (`long`, `double`, `float`), stdout, assembly helpers, slow-emulator rules, and the chapter 19 adapter.
- Preserve upstream tests; create compatible variants only where they still check the same important compiler behavior.

## Baseline

- Baseline `make test` passed 263/263 groups before changes.
- Prior turn's `make test-wacc-opt` passed, with 110 supported chapter 19 runtime tests and 57 skipped.

## Findings and experiments

- At baseline, the three explicitly skipped chapter 19 cases failed with the same compiler diagnostics both with and without `-opt`: unsupported floating literal, compound assignment to struct member, post-decrement of struct member. The upstream harness hid these diagnostics because it read only stderr at that point.

- Inventory: 1,617 chapter 1-20 C source cases excluding chapter 19 helper libraries. Reasons overlap: 441 type-filtered; 24 stdout; 11 x86 assembly helpers; 3 slow emulator; 21 explicit ignored; 124 in omitted chapters 11/13; 167 chapter 19 cases handled by the adapter; 66 chapter 20 cases beyond current Makefile scope.
- The explicit ignored list has 21 paths. Five invalid cases require floating-point values for their specific invalid-type rule; two chapter 16 valid cases contain one incidental floating literal each; chapter 17 `sizeof` cases require unsupported floating/long semantics; chapter 17 allocation pair requires unsupported long plus missing `memset`/`free` CRT functions; the ten chapter 18 library paths depend extensively on doubles/longs and x86 ABI rules.
- The two chapter 19 struct-member update cases are genuine compiler gaps under the README's supported C subset. `src/TAC.c` only handles plain variables and dereferenced pointers for compound assignment and post-increment/decrement, while member expressions on local structs lower to `SUB_OBJECT`.
- Applicable architecture documents: `docs/ISA.md` Memory section (byte addressing, load/store) and `docs/abi.md` Registers/Struct conventions/Types. No relevant behavior is unspecified for this change; it reuses existing TAC subobject instructions.
- Baseline `make test`: 263/263 passed. Baseline `make test-wacc-opt`: passed. The upstream WACC framework unit suite cannot run fully in this environment because it requires `NQCC` reference compiler and `clang`.
- Diagnostic fix in nested WACC framework: on failed emulator compilation, include status, stdout, and stderr. Verified with the intentionally unsupported NaN case: reports status 2 and the parser location from stdout.

- Added a fork regression for struct-member `/=` and post-decrement, including one-evaluation pointer-member updates. It initially failed at TAC lowering, then passed all execution backends after using existing `SUB_OBJECT` copy-from/to-offset instructions.
- Adapted two ignored chapter 16 character tests by changing only an incidental floating literal to an integer literal; all their original array/character assertions remain.
- Adapted all seven chapter 19 stdout tests into fork-owned WACC fixtures: a local `wacc_emit_char` captures the same characters in memory, and the new `main` checks the original return value plus every expected byte. The simple emulator has no stdout service. These cases now pass in both emulator modes.
- The character compound-assignment adaptation exposed a TAC interpreter mismatch: a narrow unsigned destination was not truncated/sign-normalized on assignment. The machine-code backends already passed. Added typed scalar normalization in the interpreter; `-interp` and `-interp -opt` now both return the expected value.
- While diagnosing a failing local test, observed `make test` reported a failed summary yet returned 0. The compiler Makefile now returns nonzero when passed != total.
- An intermediate `make test` passed 263/263 groups with the initial adapted cases. Later adaptations and fixes are summarized below.

## Final exclusion review

The counts below cover 1,617 WACC chapter 1–20 C source cases, excluding chapter 19 helper libraries. Reasons overlap because one source may require both an unsupported type and stdout. The Makefile's normal WACC selection is chapters 1–10, 12, and 14–18; the optimized target also uses the chapter 19 runtime adapter.

| Reason | Cases | Review |
| --- | ---: | --- |
| `long`, `double`, or `float` token (including local headers) | 441 | These types are outside the documented C subset. Four cases with an incidental `long` were adapted below; cases whose essential assertion is 64-bit or floating-point behavior remain excluded. |
| Expected stdout | 24 | Eighteen are now covered by fork-owned in-memory output/content variants, including all seven chapter 19 stdout cases. The remaining six require libc/system-call services, essential unsupported types, or both. |
| x86 assembly helper | 11 | Their stack alignment, register, and page-boundary expectations target x86 ABI details; a faithful Dioptase test needs to use the documented Dioptase ABI instead. Existing local ABI tests include `align_params.c`. |
| Emulator time limit | 3 | The large loop/memory-leak stress tests remain opt-in; shortening their loops would remove the behavior they test. One also uses an unsupported type. |
| Explicit ignored paths | 21 | Three valid cases now have fork variants (`partial_initialization`, `compound_assign_chars`, `sizeof/simple`). Invalid floating-point cases, `sizeof`'s unsigned-long result type, allocation/CRT cases, and chapter 18 long/double library cases remain excluded. |
| Chapters 11 and 13 omitted | 124 | These chapters center on unsupported long and floating-point semantics, including invalid tests that require those tokens to test their rejection. The adapted `sizeof` test preserves a useful chapter 17 form without changing an essential invalid-type test. |
| Chapter 19 assembly-based runner | 167 | The runtime adapter selects 112 original optimized programs plus eight fork programs. It skips 47 original type-dependent cases, seven original stdout cases (all adapted in the fork), and one NaN-literal case. The two struct-member update tests initially failed and were fixed in TAC lowering rather than excluded. |
| Chapter 20 beyond current target scope | 66 | These tests check register allocation/coalescing and x86 hard-register behavior; they cannot validate the present optimizer pipeline by return code alone. |

The fork variants preserve the original WACC source files and check the behavior their source intended. They now live under `tests/writing-a-c-compiler-tests/tests/chapter_18/valid/dioptase/` and `chapter_19/dioptase/`:

- Two ignored chapter 16 character tests replace one incidental floating literal with an integer literal and retain every original character/array assertion.
- `wacc_sizeof_type_and_expression.c` replaces the unsupported `double` expression with a `char` object, preserving the test's two `sizeof` forms and distinct target widths.
- Four type-filtered tests replace one incidental `long` in pointer shifting, character promotion, union self-reference, or union conditional evaluation, preserving their original assertions.
- Eighteen stdout cases use in-memory output capture or compare the string bytes in memory: seven chapter 19 optimization cases, six chapter 9/10/14/17 `putchar` cases, four chapter 16 string cases, and the chapter 18 static-struct state case. The chapter 18 variant replaces libc allocation with static objects; it keeps the original call sequence and checks the exact 41-byte output.
- The fork-local `struct_member_updates.c` regression covers the two chapter 19 compiler gaps, including single evaluation of the left-side pointer expression.

The six stdout paths without fork variants are the chapter 9 `system_call` library/client pair, chapter 16 `standard_library_calls.c`, and the chapter 18 `opaque_struct` library/client pair plus `incomplete_structs.c`. The system-call pair specifically tests host `putchar` linking; the standard-library case specifically tests libc `strcmp`, `strlen`, `atoi`, and `puts`. The chapter 18 cases combine libc calls with essential unsupported `long`/`double` declarations. A rewrite would change their principal contract, so they remain visible as exclusions.

## Additional bugs found by adapted tests

- The TAC interpreter did not normalize narrow scalar assignments to the target width; the ignored character compound-assignment test exposed it. Normalization now truncates unsigned scalars and sign-extends signed scalars.
- The TAC interpreter zeroed static aggregates only at the size of their innermost element. That left other member offsets uninitialized. The adapted chapter 18 static-struct test caught this; initialization now covers every byte offset.
- `typecheck_func` inspected the function type tag itself when checking for an array return, which can never match `ARRAY_TYPE`. The unchanged WACC `chapter_15/invalid_types/function_returns_array.c` passed TAC validation. It now rejects the function's actual array return type with a source-located error.
- The compiler's `make test` target previously reported failed groups but exited with status zero. It now returns nonzero if the summary has any failure.
- The complete `make test-tac-wacc-opt` target remains limited by the TAC runner's absent libc implementations. After the array-return type fix it passes through chapter 15, then chapter 16 reports eight `strcmp`/`puts` runtime failures. These are unimplemented runtime services, not changes to the upstream expectations. The fork variants test the corresponding compiler behavior without those services.
- A fork fixture's nested object-like macro exposed another documented-feature gap: expansion stopped after one replacement. The preprocessor now rescans macro text, preserves quoted literals and invocation-site source mapping, and stops direct or indirect recursion. `tests/preprocess/define_nested.c` checks all three behaviors; the fork's static-struct fixture again uses the nested macro.

## Verification

- After moving the adapted programs into the fork, `make test` and `make test-release` passed 264/264 groups. TAC execution passed 61/61 fixtures in both modes (one separate fixture is intentionally skipped); simple and full emulator execution each passed 62/62 fixtures in both modes.
- After the move, `make test-wacc-opt` and `make test-wacc-release-opt` passed all selected chapters. Each included 120/120 chapter 19 runtime programs under `-opt` and 219/219 chapter 18 programs, including all fork variants.
- `make test-tac-wacc-opt WACC_CORE_CHAPTER=15 WACC_EXTRA_CHAPTERS= WACC_ARGS=--latest-only`: 67/67 passed, including the unchanged invalid array-return declaration.
- The full `make test-tac-wacc-opt` reaches chapter 16 and fails eight cases that call unavailable TAC interpreter libc functions (`strcmp` or `puts`). These are reported above as runtime limitations; the exact fork variants exercise the supported compiler behavior.

## Fork ownership and full-emulator validation

The 26 adapted/regression programs now live in the WACC fork: 18 in
`tests/chapter_18/valid/dioptase/` and eight in `tests/chapter_19/dioptase/`.
Their expected zero exit statuses live in `dioptase_expected_results.json`;
`test_framework/basic.py` loads those expectations and selects the variants
only for Dioptase emulator runs. The fork keeps the original cases unchanged.
All 26 variants also returned zero without output under host GCC with undefined
behavior sanitization enabled.

`make test-wacc` passed after the move (219 chapter 18 cases).
`make test-wacc-kernel-opt WACC_ARGS=--skip-libraries` passed through the full
emulator, including 207 chapter 18 cases. The default kernel target encountered
four existing chapter 18 library tests with unresolved `calloc`/`memcmp`
symbols; no fork variant failed. The WACC fork's own `test_toplevel` unit tests
require the unavailable `NQCC` reference compiler in this environment.
