#!/usr/bin/env python3
"""
Purpose: Wrapper compiler that adapts the Writing-a-C-Compiler test suite to use the TAC interpreter.
Inputs: Compiler-style arguments including a single C source file and optional -D defines.
Outputs: Creates an executable script for valid programs or returns non-zero on compile failures.
Invariants/Assumptions: The real compiler is provided via DIOPTASE_BCC and supports -interp.
                      Uses a host gcc-style preprocessor to expand includes/macros first.
                      Uses the host compiler for -c output because the TAC compiler has no backend.
                      Optionally uses host GCC for known slow runtime tests when enabled.
"""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys
from typing import List, Optional, Tuple


# Purpose: Define wrapper exit codes for common failure modes.
# Inputs/Outputs: Constants returned by main when wrapper setup fails.
# Invariants/Assumptions: Exit codes are non-zero and do not overlap compiler errors.
EXIT_USAGE = 64
EXIT_UNSUPPORTED = 65
EXIT_WRITE_FAILED = 66
EXIT_PREPROCESS_FAILED = 67

# Purpose: Enumerate accepted wrapper flags for the test suite bridge.
# Inputs/Outputs: Used by argument parsing to classify options.
# Invariants/Assumptions: -lm is ignored; -D macros pass through to the real compiler.
IGNORED_FLAGS = {"-lm"}
UNSUPPORTED_FLAGS = {"-S", "--lex", "--parse", "--validate", "--tacky", "--codegen"}
# Purpose: Allow overriding the host preprocessor command for portability.
# Inputs/Outputs: Read by get_preprocessor_command to pick a gcc-compatible preprocessor.
# Invariants/Assumptions: The command accepts -E/-P and -D flags.
PREPROCESSOR_ENV = "DIOPTASE_GCC"
PREPROCESSOR_FLAGS = ["-E", "-P"]
# Purpose: Optionally swap slow runtime tests to host GCC execution.
# Inputs/Outputs: Controlled by SLOW_RUNTIME_ENV and the SLOW_RUNTIME_TESTS list.
# Invariants/Assumptions: Used to avoid interpreter timeouts for long-running programs.
SLOW_RUNTIME_ENV = "DIOPTASE_TACC_GCC_RUNTIME"
SLOW_RUNTIME_TESTS = {"empty_loop_body.c", "test_for_memory_leaks.c"}
SLOW_RUNTIME_ENV = "DIOPTASE_TACC_GCC_RUNTIME"
SLOW_RUNTIME_TESTS = {"empty_loop_body.c", "test_for_memory_leaks.c"}


def get_compiler_path(env_var: str) -> Path:
    """
    Purpose: Resolve the real compiler path from the environment.
    Inputs: env_var names the environment variable to consult.
    Outputs: Returns an absolute Path to the compiler executable.
    Invariants/Assumptions: The environment variable is set and points to an executable file.
    """
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


def parse_args(argv: List[str]) -> Tuple[Path, Path, List[str], bool]:
    """
    Purpose: Parse wrapper arguments and extract the source and output paths.
    Inputs: argv is the argument vector excluding the program name.
    Outputs: Returns (source_path, output_path, pass_through_args, compile_only).
    Invariants/Assumptions: Exactly one C source file is provided and any -o has a value.
    """
    source: Optional[Path] = None
    output: Optional[Path] = None
    pass_through: List[str] = []
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
        if arg.startswith("-D"):
            pass_through.append(arg)
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

    return source, output, pass_through, compile_only


def get_preprocessor_command(env_var: str) -> str:
    """
    Purpose: Resolve the preprocessor command from the environment.
    Inputs: env_var names the environment variable to consult.
    Outputs: Returns the executable name to invoke.
    Invariants/Assumptions: The command is a single executable name, not a full shell string.
    """
    raw = os.environ.get(env_var)
    if raw is None or raw.strip() == "":
        return "gcc"
    return raw.strip()


def build_preprocessed_path(output: Path) -> Path:
    """
    Purpose: Choose a stable on-disk path for the preprocessed source file.
    Inputs: output is the expected executable path for the test case.
    Outputs: Returns a sibling path with a .i suffix appended to the output name.
    Invariants/Assumptions: The test harness cleans up non-source artifacts afterward.
    """
    return output.with_name(output.name + ".i")


def should_use_gcc_runtime(source: Path) -> bool:
    """
    Purpose: Decide whether to run a test binary via host GCC for slow cases.
    Inputs: source is the original C source file path.
    Outputs: Returns true when GCC runtime should be used instead of the TAC interpreter.
    Invariants/Assumptions: Activated only when SLOW_RUNTIME_ENV is set.
    """
    enabled = os.environ.get(SLOW_RUNTIME_ENV)
    if enabled is None or enabled.strip() == "":
        return False
    return source.name in SLOW_RUNTIME_TESTS


def preprocess_source(preprocessor: str,
                      source: Path,
                      output: Path,
                      pass_through: List[str]) -> subprocess.CompletedProcess[str]:
    """
    Purpose: Run the host preprocessor to expand includes and macros.
    Inputs: preprocessor is the command name; source is the input file; output is the destination.
    Outputs: Returns the CompletedProcess containing stdout/stderr and exit status.
    Invariants/Assumptions: Uses gcc-style -E/-P flags to suppress #line directives.
    """
    args = [preprocessor, *PREPROCESSOR_FLAGS, *pass_through, str(source), "-o", str(output)]
    return subprocess.run(args, capture_output=True, text=True)


def remove_path(path: Path) -> None:
    """
    Purpose: Remove a file if it exists to keep test output directories clean.
    Inputs: path is the file path to remove.
    Outputs: None; errors are ignored if the file is missing.
    Invariants/Assumptions: Only files (not directories) are removed.
    """
    try:
        if path.is_file() or path.is_symlink():
            path.unlink()
    except FileNotFoundError:
        pass


def write_exec_script(output_path: Path,
                      compiler: Path,
                      pass_through: List[str],
                      input_path: Path) -> None:
    """
    Purpose: Emit an executable script that runs the TAC interpreter for a test case.
    Inputs: output_path is the executable path; compiler is the real compiler; pass_through holds -D args; input_path is the preprocessed file.
    Outputs: Writes the script to output_path with executable permissions.
    Invariants/Assumptions: The host can execute Python scripts with /usr/bin/env.
    """
    args_literal = repr([str(compiler), "-interp", *pass_through, str(input_path)])
    script = f"""#!/usr/bin/env python3
import os
import subprocess
import sys

EXIT_PARSE_FAILURE = 1
EXIT_CODE_MASK = 0xFF
RESULT_ENV = "DIOPTASE_TACC_RESULT_STDERR"

# Purpose: Execute the TAC interpreter and return its exit status as a process code.
# Inputs: Uses an embedded compiler command line for the source under test.
# Outputs: Exits with the interpreted main() result modulo 256; prints diagnostics on failure.
# Invariants/Assumptions: The compiler prints exactly one integer result on stderr when RESULT_ENV is set.
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
    """
    Purpose: Drive the compiler wrapper by validating arguments and emitting the run script.
    Inputs: argv is the argument list including the program name.
    Outputs: Returns a process exit code for the test harness.
    Invariants/Assumptions: The real compiler path is configured via DIOPTASE_BCC.
    """
    try:
        compiler = get_compiler_path("DIOPTASE_BCC")
        source, output, pass_through, compile_only = parse_args(argv[1:])
    except RuntimeError as exc:
        sys.stderr.write(f"TAC runner error: {exc}\n")
        return EXIT_UNSUPPORTED
    except ValueError as exc:
        sys.stderr.write(f"TAC runner usage error: {exc}\n")
        return EXIT_USAGE

    preprocessor = get_preprocessor_command(PREPROCESSOR_ENV)
    preprocessed = build_preprocessed_path(output)
    try:
        preprocess_result = preprocess_source(preprocessor, source, preprocessed, pass_through)
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

    result = subprocess.run([str(compiler), *pass_through, str(preprocessed)])
    if result.returncode != 0:
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
            write_exec_script(output, compiler, pass_through, preprocessed)
    except OSError as exc:
        sys.stderr.write(f"TAC runner failed to write executable {output}: {exc}\n")
        remove_path(output)
        remove_path(preprocessed)
        return EXIT_WRITE_FAILED

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
