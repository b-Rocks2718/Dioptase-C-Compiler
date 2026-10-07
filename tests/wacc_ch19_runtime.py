#!/usr/bin/env python3
"""Run WACC chapter 19 programs through the Dioptase emulator.

The upstream chapter 19 runner inspects x86 assembly for many tests. This
adapter uses its runtime expectations and skip rules without those x86 checks.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import unittest


WACC_ROOT = Path(__file__).resolve().parent / "writing-a-c-compiler-tests"
sys.path.insert(0, str(WACC_ROOT))

from test_framework import basic  # noqa: E402

# These cases cannot reach code generation even without optimizations. Keep
# the exclusions explicit so adding support to the compiler is easy to track.
UNSUPPORTED_PROGRAMS = {
    "constant_folding/all_types/extra_credit/cast_nan_not_executed.c":
        "floating-point literal syntax is unsupported",
}


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    """Split runner options from flags passed to the compiler after --."""
    argv = sys.argv[1:]
    compiler_options: list[str] = []
    if "--" in argv:
        separator = argv.index("--")
        compiler_options = argv[separator + 1 :]
        argv = argv[:separator]

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--skip-types", nargs="*", default=[])
    parser.add_argument("--skip-libraries", action="store_true")
    parser.add_argument("--skip-stdout", action="store_true")
    parser.add_argument("--failfast", action="store_true")
    parser.add_argument("--verbose", action="count", default=0)
    parser.add_argument("--include-unsupported", action="store_true")
    for feature in ("bitwise", "compound", "increment", "goto", "switch", "nan", "union"):
        parser.add_argument(f"--{feature}", action="store_true")
    return parser.parse_args(argv), compiler_options


def main() -> int:
    args, compiler_options = parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        raise SystemExit(f"WACC chapter 19: compiler does not exist: {compiler}")
    if not compiler_options:
        raise SystemExit("WACC chapter 19: expected optimization flags after --")

    enabled_features = basic.ExtraCredit.NONE
    for feature in ("bitwise", "compound", "increment", "goto", "switch", "nan", "union"):
        if getattr(args, feature):
            enabled_features |= basic.ExtraCredit[feature.upper()]

    chapter_dir = basic.TEST_DIR / "chapter_19"
    skip_type_pattern = basic.build_type_skip_pattern(args.skip_types)
    test_methods = {}
    skipped = 0
    unsupported = []
    for program in sorted(chapter_dir.rglob("*.c")):
        if "helper_libs" in program.parts:
            continue
        if basic.should_skip_program(
            program,
            skip_libraries=args.skip_libraries,
            skip_stdout=args.skip_stdout,
            skip_type_pattern=skip_type_pattern,
        ) or basic.excluded_extra_credit(program, enabled_features):
            skipped += 1
            continue
        relative = program.relative_to(chapter_dir).as_posix()
        if not args.include_unsupported and relative in UNSUPPORTED_PROGRAMS:
            unsupported.append((relative, UNSUPPORTED_PROGRAMS[relative]))
            skipped += 1
            continue
        name = "test_" + program.relative_to(chapter_dir).with_suffix("").as_posix()
        test_methods[name] = basic.make_test_run(program)

    if not test_methods:
        raise SystemExit("WACC chapter 19: no runtime tests selected; check skip options")

    test_case = type(
        "TestChapter19Runtime",
        (basic.TestChapter,),
        {
            "test_dir": chapter_dir,
            "cc": compiler,
            "options": compiler_options,
            "exit_stage": None,
            "error_codes": [],
            **test_methods,
        },
    )
    print(f"WACC chapter 19 runtime: {len(test_methods)} selected, {skipped} skipped", flush=True)
    for path, reason in unsupported:
        print(f"  unsupported: {path} ({reason})", flush=True)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(test_case)
    result = unittest.TextTestRunner(verbosity=args.verbose, failfast=args.failfast).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
