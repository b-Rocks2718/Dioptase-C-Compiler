#!/usr/bin/env python3
"""Adapt Writing-a-C-Compiler tests to execute through the TAC interpreter.

The wrapper preprocesses one C source file, validates it with the compiler from
``DIOPTASE_BCC``, and emits an executable launcher. Compile-only invocations and
explicitly enabled slow tests use the host compiler because TAC has no backend.
"""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys
from typing import List, Optional, Tuple


# Define wrapper exit codes for common failure modes.
# Exit codes are non-zero and do not overlap compiler errors.
EXIT_USAGE = 64
EXIT_UNSUPPORTED = 65
EXIT_WRITE_FAILED = 66
EXIT_PREPROCESS_FAILED = 67

# Enumerate accepted wrapper flags for the test suite bridge.
IGNORED_FLAGS = {"-lm"}
UNSUPPORTED_FLAGS = {"-S", "--lex", "--parse", "--validate", "--tacky", "--codegen"}
COMPILER_ONLY_FLAGS = {
    "-constant-fold",
    "-copy-prop",
    "-dead-code",
    "-dead-store",
    "-inline",
    "-opt",
    "-peephole",
    "-reg-alloc",
    "-tail-call",
}
# Allow overriding the host preprocessor command for portability.
PREPROCESSOR_ENV = "DIOPTASE_GCC"
PREPROCESSOR_FLAGS = ["-E", "-P"]
# Optionally swap slow runtime tests to host GCC execution.
SLOW_RUNTIME_ENV = "DIOPTASE_TACC_GCC_RUNTIME"
SLOW_RUNTIME_TESTS = {"empty_loop_body.c", "test_for_memory_leaks.c"}
SLOW_RUNTIME_ENV = "DIOPTASE_TACC_GCC_RUNTIME"
SLOW_RUNTIME_TESTS = {"empty_loop_body.c", "test_for_memory_leaks.c"}


def get_compiler_path(env_var: str) -> Path:
    """Resolve and validate the compiler executable named by an environment variable."""
    raw = os.environ.get(env_var)
    if raw is None or raw.strip() == "":
        raise ValueError(
            f"{env_var} is not set; set it to the path of the Dioptase C compiler binary"
        )
    path = Path(raw).expanduser().resolve()
    if not path.exists():
        raise ValueError(f"{env_var} points to missing compiler binary: {path}")
    if not os.access(path, os.X_OK):
        raise ValueError(f"{env_var} compiler binary is not executable: {path}")
    return path


def parse_args(argv: List[str]) -> Tuple[Path, Path, List[str], List[str], bool]:
    """Parse one source file plus the subset of compiler flags supported by the wrapper."""
    source: Optional[Path] = None
    output: Optional[Path] = None
    preprocessor_options: List[str] = []
    compiler_options: List[str] = []
    compile_only = False
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "-c":
            compile_only = True
            i += 1
            continue
        if arg == "-o":
            if i + 1 >= len(argv):
                raise ValueError("missing output path after -o")
            output = Path(argv[i + 1]).expanduser().resolve()
            i += 2
            continue
        if arg in UNSUPPORTED_FLAGS:
            raise RuntimeError(f"unsupported compiler option for TAC runner: {arg}")
        if arg in IGNORED_FLAGS:
            i += 1
            continue
        if arg in COMPILER_ONLY_FLAGS:
            compiler_options.append(arg)
            i += 1
            continue
        if arg.startswith("-D"):
            preprocessor_options.append(arg)
            compiler_options.append(arg)
            i += 1
            continue
        if arg.startswith("-"):
            raise RuntimeError(f"unsupported compiler option for TAC runner: {arg}")
        if source is not None:
            raise ValueError("multiple source files are not supported by the TAC runner")
        source = Path(arg).expanduser().resolve()
        i += 1

    if source is None:
        raise ValueError("no source file provided to TAC compiler wrapper")

    if output is None:
        output = source.with_suffix(".o" if compile_only else "")

    return source, output, preprocessor_options, compiler_options, compile_only


def get_preprocessor_command(env_var: str) -> str:
    """Select the configured preprocessor executable, defaulting to ``gcc``."""
    raw = os.environ.get(env_var)
    if raw is None or raw.strip() == "":
        return "gcc"
    return raw.strip()


def build_preprocessed_path(output: Path) -> Path:
    """Place the temporary preprocessed source beside the requested output."""
    return output.with_name(output.name + ".i")


def should_use_gcc_runtime(source: Path) -> bool:
    """Select the opt-in host runtime for test cases that are prohibitively slow in TAC."""
    enabled = os.environ.get(SLOW_RUNTIME_ENV)
    if enabled is None or enabled.strip() == "":
        return False
    return source.name in SLOW_RUNTIME_TESTS


# Run the host preprocessor without shell interpretation and capture diagnostics.
def preprocess_source(preprocessor: str,
                      source: Path,
                      output: Path,
                      pass_through: List[str]) -> subprocess.CompletedProcess[str]:
    """Expand includes and macros while suppressing host ``#line`` directives."""
    args = [preprocessor, *PREPROCESSOR_FLAGS, *pass_through, str(source), "-o", str(output)]
    return subprocess.run(args, capture_output=True, text=True)


def remove_path(path: Path) -> None:
    """Remove a generated file or symlink while tolerating a missing path."""
    try:
        if path.is_file() or path.is_symlink():
            path.unlink()
    except FileNotFoundError:
        pass


# Emit an executable launcher that converts the interpreted main result to an exit status.
def write_exec_script(output_path: Path,
                      compiler: Path,
                      compiler_options: List[str],
                      input_path: Path) -> None:
    """Write the TAC-interpreter launcher and mark it executable for the test harness."""
    args_literal = repr([str(compiler), "-interp", *compiler_options, str(input_path)])
    script = f"""#!/usr/bin/env python3
import os
import subprocess
import sys

EXIT_PARSE_FAILURE = 1
EXIT_CODE_MASK = 0xFF
RESULT_ENV = "DIOPTASE_TACC_RESULT_STDERR"

# Execute the TAC interpreter and return its exit status as a process code.
# Exits with the interpreted main() result modulo 256; prints diagnostics on failure.
def main() -> int:
    args = {args_literal}
    env = dict(os.environ)
    env[RESULT_ENV] = "1"
    result = subprocess.run(args, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode
    stderr_lines = result.stderr.splitlines()
    if not stderr_lines:
        sys.stdout.write(result.stdout)
        sys.stderr.write("TAC interpreter produced no result\\n")
        return EXIT_PARSE_FAILURE
    try:
        value = int(stderr_lines[-1].strip())
    except ValueError:
        sys.stdout.write(result.stdout)
        sys.stderr.write(f"Invalid TAC interpreter output: {{result.stderr!r}}\\n")
        return EXIT_PARSE_FAILURE
    if len(stderr_lines) > 1:
        sys.stderr.write("\\n".join(stderr_lines[:-1]) + "\\n")
    sys.stdout.write(result.stdout)
    return value & EXIT_CODE_MASK


if __name__ == "__main__":
    sys.exit(main())
"""
    output_path.write_text(script, encoding="utf-8")
    output_path.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
                      stat.S_IRGRP | stat.S_IXGRP |
                      stat.S_IROTH | stat.S_IXOTH)


def main(argv: List[str]) -> int:
    """Validate the source and emit either a host object or TAC launcher."""
    try:
        compiler = get_compiler_path("DIOPTASE_BCC")
        source, output, preprocessor_options, compiler_options, compile_only = parse_args(argv[1:])
    except RuntimeError as exc:
        sys.stderr.write(f"TAC runner error: {exc}\n")
        return EXIT_UNSUPPORTED
    except ValueError as exc:
        sys.stderr.write(f"TAC runner usage error: {exc}\n")
        return EXIT_USAGE

    preprocessor = get_preprocessor_command(PREPROCESSOR_ENV)
    preprocessed = build_preprocessed_path(output)
    try:
        preprocess_result = preprocess_source(
            preprocessor, source, preprocessed, preprocessor_options
        )
    except FileNotFoundError:
        sys.stderr.write(
            f"TAC runner error: preprocessor '{preprocessor}' was not found in PATH\n"
        )
        remove_path(preprocessed)
        return EXIT_PREPROCESS_FAILED

    if preprocess_result.returncode != 0:
        sys.stdout.write(preprocess_result.stdout)
        sys.stderr.write(preprocess_result.stderr)
        remove_path(preprocessed)
        return preprocess_result.returncode

    # Stop after TAC lowering to avoid invoking the assembler during wrapper compilation.
    compile_args = [str(compiler), *compiler_options, "-tac", str(preprocessed)]
    result = subprocess.run(compile_args, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        remove_path(output)
        remove_path(output.with_suffix(".s"))
        remove_path(preprocessed)
        return result.returncode

    try:
        if compile_only:
            # Use host GCC for object emission since the TAC compiler has no backend.
            cc_result = subprocess.run(
                [preprocessor, "-c", str(preprocessed), "-o", str(output)],
                capture_output=True,
                text=True,
            )
            if cc_result.returncode != 0:
                sys.stdout.write(cc_result.stdout)
                sys.stderr.write(cc_result.stderr)
                remove_path(output)
                remove_path(preprocessed)
                return cc_result.returncode
            remove_path(preprocessed)
        elif should_use_gcc_runtime(source):
            cc_result = subprocess.run(
                [preprocessor, str(preprocessed), "-o", str(output)],
                capture_output=True,
                text=True,
            )
            if cc_result.returncode != 0:
                sys.stdout.write(cc_result.stdout)
                sys.stderr.write(cc_result.stderr)
                remove_path(output)
                remove_path(preprocessed)
                return cc_result.returncode
            remove_path(preprocessed)
        else:
            write_exec_script(output, compiler, compiler_options, preprocessed)
    except OSError as exc:
        sys.stderr.write(f"TAC runner failed to write executable {output}: {exc}\n")
        remove_path(output)
        remove_path(preprocessed)
        return EXIT_WRITE_FAILED

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
