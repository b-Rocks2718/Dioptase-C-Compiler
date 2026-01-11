#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Note: These tests intentionally rely on POSIX process APIs and a host C compiler
// to compare emulator execution results against native execution.

// Purpose: Configure test runner paths and sizing limits.
// Inputs/Outputs: Constants used by the emulator execution tests.
// Invariants/Assumptions: Paths are relative to the compiler root.
static const char* kEmuExecBuildDir = "build";
static const char* kEmuExecOutDir = "build/emu_exec";
static const char* kEmuExecCompilerEnv = "TAC_TEST_CC";
static const char* kEmuExecBccEnv = "DIOPTASE_BCC";
static const char* kEmuExecEmulatorEnv = "DIOPTASE_EMULATOR_SIMPLE";
static const char* kEmuExecEmuMaxCyclesEnv = "TAC_EMU_MAX_CYCLES";
static const char* kEmuExecCStandard = "-std=c11";
static const size_t kEmuExecMaxPath = 512; // Supports test paths plus output suffixes.
static const size_t kEmuExecMaxOutput = 256; // Enough for emulator output + newline.
static const uint32_t kEmuExecEmuMaxCyclesDefault = 100000; // Bounds emulator runtime for stuck programs.

enum { kEmuExecDefaultBccPathCount = 2 };
static const char* const kEmuExecDefaultBccPaths[kEmuExecDefaultBccPathCount] = {
    "build/debug/bcc",
    "build/release/bcc",
};

enum { kEmuExecDefaultEmulatorPathCount = 2 };
static const char* const kEmuExecDefaultEmulatorPaths[kEmuExecDefaultEmulatorPathCount] = {
    "../../Dioptase-Emulators/Dioptase-Emulator-Simple/target/debug/Dioptase-Emulator-Simple",
    "../../Dioptase-Emulators/Dioptase-Emulator-Simple/target/release/Dioptase-Emulator-Simple",
};

// Purpose: Describe an emulator execution test case.
// Inputs/Outputs: name identifies the test; path points to the C source.
// Invariants/Assumptions: path is a NUL-terminated file system path.
struct EmuExecTest {
  const char* name;
  const char* path;
};

static const struct EmuExecTest kEmuExecTests[] = {
    {"arithmetic", "tests/exec/arithmetic.c"},
    {"logical", "tests/exec/logical.c"},
    {"loops", "tests/exec/loops.c"},
    {"switch", "tests/exec/switch.c"},
    {"goto", "tests/exec/goto.c"},
    {"functions", "tests/exec/functions.c"},
    {"pointers", "tests/exec/pointers.c"},
    {"pointer_store", "tests/exec/pointer_store.c"},
    {"global_pointer_basic", "tests/exec/global_pointer_basic.c"},
    {"global_pointer_cross", "tests/exec/global_pointer_cross.c"},
    {"globals", "tests/exec/globals.c"},
    {"conditional", "tests/exec/conditional.c"},
};

// Purpose: Print a formatted failure message for a test stage.
// Inputs: test is the test name; stage is the pipeline stage; fmt is the detail.
// Outputs: Writes a diagnostic message to stderr.
// Invariants/Assumptions: fmt is a printf-style format string.
static void emu_exec_error(const char* test, const char* stage, const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "Emu exec test %s failed at %s: ", test, stage);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Purpose: Ensure the output directory exists.
// Inputs: path is the directory to create.
// Outputs: Returns true on success or if it already exists.
// Invariants/Assumptions: Uses POSIX mkdir semantics.
static bool emu_exec_ensure_dir(const char* path) {
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

// Purpose: Check whether a path points to an executable file.
// Inputs: path is the filesystem path to check.
// Outputs: Returns true when the file is executable.
// Invariants/Assumptions: Uses POSIX access().
static bool emu_exec_is_executable(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return false;
  }
  return access(path, X_OK) == 0;
}

// Purpose: Parse a non-zero u32 from a decimal string.
// Inputs: text is the string to parse.
// Outputs: Returns true on success and fills out_value.
// Invariants/Assumptions: Rejects values outside the uint32_t range or zero.
static bool emu_exec_parse_u32_nonzero(const char* text, uint32_t* out_value) {
  if (text == NULL || out_value == NULL) {
    return false;
  }
  while (isspace((unsigned char)*text)) {
    text++;
  }
  errno = 0;
  char* end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  if (text == end || errno != 0) {
    return false;
  }
  while (end != NULL && isspace((unsigned char)*end)) {
    end++;
  }
  if (end != NULL && *end != '\0') {
    return false;
  }
  if (value == 0 || value > UINT32_MAX) {
    return false;
  }
  *out_value = (uint32_t)value;
  return true;
}

// Purpose: Select a tool path from an environment override or defaults.
// Inputs: env_name is the environment variable to check; defaults list fallbacks.
// Outputs: Returns a usable tool path or NULL if none are available.
// Invariants/Assumptions: Defaults are relative to the compiler root.
static const char* emu_exec_select_tool(const char* env_name,
                                        const char* const* defaults,
                                        size_t default_count) {
  const char* env = getenv(env_name);
  if (env != NULL && env[0] != '\0') {
    if (emu_exec_is_executable(env)) {
      return env;
    }
    fprintf(stderr, "Emu exec tests: %s=%s is not executable\n", env_name, env);
    return NULL;
  }

  for (size_t i = 0; i < default_count; i++) {
    if (emu_exec_is_executable(defaults[i])) {
      return defaults[i];
    }
  }

  return NULL;
}

// Purpose: Spawn a child process and capture its exit code.
// Inputs: argv is the command array; search_path selects execvp vs execv.
// Outputs: Returns true on success and fills exit_code.
// Invariants/Assumptions: Uses POSIX fork/exec/wait APIs.
static bool emu_exec_run_process(const char* const* argv, bool search_path, int* exit_code) {
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "fork failed for %s: %s\n", argv[0], strerror(errno));
    return false;
  }
  if (pid == 0) {
    if (search_path) {
      execvp(argv[0], (char* const*)argv);
    } else {
      execv(argv[0], (char* const*)argv);
    }
    fprintf(stderr, "exec failed for %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "waitpid failed for %s: %s\n", argv[0], strerror(errno));
    return false;
  }
  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return true;
  }
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "process %s terminated by signal %d\n", argv[0], WTERMSIG(status));
  }
  return false;
}

// Purpose: Spawn a child process and capture stdout output.
// Inputs: argv is the command array; search_path selects execvp vs execv.
// Outputs: Returns true on success, fills exit_code, and writes output to buffer.
// Invariants/Assumptions: buffer_size must be > 0; output is NUL-terminated.
static bool emu_exec_run_process_capture(const char* const* argv,
                                         bool search_path,
                                         char* buffer,
                                         size_t buffer_size,
                                         int* exit_code) {
  if (buffer == NULL || buffer_size == 0) {
    fprintf(stderr, "emu_exec_run_process_capture: invalid buffer\n");
    return false;
  }

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    fprintf(stderr, "pipe failed for %s: %s\n", argv[0], strerror(errno));
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "fork failed for %s: %s\n", argv[0], strerror(errno));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return false;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
      fprintf(stderr, "dup2 failed for %s: %s\n", argv[0], strerror(errno));
      _exit(127);
    }
    close(pipe_fds[1]);
    if (search_path) {
      execvp(argv[0], (char* const*)argv);
    } else {
      execv(argv[0], (char* const*)argv);
    }
    fprintf(stderr, "exec failed for %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }

  close(pipe_fds[1]);
  size_t total = 0;
  ssize_t read_count = 0;
  while ((read_count = read(pipe_fds[0], buffer + total, buffer_size - 1 - total)) > 0) {
    total += (size_t)read_count;
    if (total >= buffer_size - 1) {
      break;
    }
  }
  close(pipe_fds[0]);
  buffer[total] = '\0';

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "waitpid failed for %s: %s\n", argv[0], strerror(errno));
    return false;
  }
  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return true;
  }
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "process %s terminated by signal %d\n", argv[0], WTERMSIG(status));
  }
  return false;
}

// Purpose: Compile a test with the host compiler for reference output.
// Inputs: source_path is the test file; out_path is the output binary.
// Outputs: Returns true if the compile succeeds; exit_code receives the process status.
// Invariants/Assumptions: Uses gcc/cc via PATH to match host behavior.
static bool emu_exec_compile_with_host(const char* source_path,
                                       const char* out_path,
                                       int* exit_code) {
  const char* compiler = getenv(kEmuExecCompilerEnv);
  if (compiler == NULL || compiler[0] == '\0') {
    compiler = "gcc";
  }
  const char* argv[] = {compiler, kEmuExecCStandard, source_path, "-o", out_path, NULL};
  int local_exit = 0;
  if (!emu_exec_run_process(argv, true, &local_exit)) {
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  if (exit_code != NULL) {
    *exit_code = local_exit;
  }
  return local_exit == 0;
}

// Purpose: Compile a test with the Dioptase compiler to a hex image.
// Inputs: source_path is the test file; out_path is the output hex file.
// Outputs: Returns true if the compile succeeds; exit_code receives the process status.
// Invariants/Assumptions: Uses DIOPTASE_BCC or local build paths.
static bool emu_exec_compile_with_bcc(const char* source_path,
                                      const char* out_path,
                                      int* exit_code) {
  const char* compiler = emu_exec_select_tool(kEmuExecBccEnv,
                                              kEmuExecDefaultBccPaths,
                                              kEmuExecDefaultBccPathCount);
  if (compiler == NULL) {
    fprintf(stderr, "Emu exec tests: unable to find compiler binary (set %s)\n",
            kEmuExecBccEnv);
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  const char* argv[] = {compiler, source_path, "-o", out_path, NULL};
  int local_exit = 0;
  if (!emu_exec_run_process(argv, true, &local_exit)) {
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  if (exit_code != NULL) {
    *exit_code = local_exit;
  }
  return local_exit == 0;
}

// Purpose: Parse a hex string result from the emulator into a signed int.
// Inputs: text is the emulator output buffer.
// Outputs: Returns true on success and fills out_value.
// Invariants/Assumptions: Emulator output is a single hex number.
static bool emu_exec_parse_emulator_result(const char* text, int* out_value) {
  if (text == NULL || out_value == NULL) {
    return false;
  }
  while (isspace((unsigned char)*text)) {
    text++;
  }
  errno = 0;
  char* end = NULL;
  unsigned long value = strtoul(text, &end, 16);
  if (text == end || errno != 0) {
    return false;
  }
  while (end != NULL && isspace((unsigned char)*end)) {
    end++;
  }
  if (end != NULL && *end != '\0') {
    return false;
  }
  *out_value = (int)(int32_t)value;
  return true;
}

// Purpose: Run the Dioptase emulator on a hex image and capture its result.
// Inputs: hex_path is the hex file to execute.
// Outputs: Returns true on success and fills out_result.
// Invariants/Assumptions: Emulator prints the return value as a hex number.
static bool emu_exec_run_emulator(const char* hex_path, int* out_result) {
  const char* emulator = emu_exec_select_tool(kEmuExecEmulatorEnv,
                                              kEmuExecDefaultEmulatorPaths,
                                              kEmuExecDefaultEmulatorPathCount);
  if (emulator == NULL) {
    fprintf(stderr, "Emu exec tests: unable to find emulator binary (set %s)\n",
            kEmuExecEmulatorEnv);
    return false;
  }
  uint32_t max_cycles = kEmuExecEmuMaxCyclesDefault;
  const char* max_cycles_env = getenv(kEmuExecEmuMaxCyclesEnv);
  if (max_cycles_env != NULL && max_cycles_env[0] != '\0') {
    if (!emu_exec_parse_u32_nonzero(max_cycles_env, &max_cycles)) {
      fprintf(stderr, "Emu exec tests: invalid %s=%s (expected non-zero u32)\n",
              kEmuExecEmuMaxCyclesEnv, max_cycles_env);
      return false;
    }
  }

  char max_cycles_arg[32];
  snprintf(max_cycles_arg, sizeof(max_cycles_arg), "%u", max_cycles);
  const char* argv[] = {emulator, "--max-cycles", max_cycles_arg, hex_path, NULL};
  char buffer[kEmuExecMaxOutput];
  int exit_code = 0;
  if (!emu_exec_run_process_capture(argv, true, buffer, sizeof(buffer), &exit_code)) {
    return false;
  }
  if (exit_code != 0) {
    fprintf(stderr,
            "emulator exited with status %d (max cycles %u; override via %s)\n",
            exit_code, max_cycles, kEmuExecEmuMaxCyclesEnv);
    return false;
  }
  if (!emu_exec_parse_emulator_result(buffer, out_result)) {
    fprintf(stderr, "unable to parse emulator output: %s\n", buffer);
    return false;
  }
  return true;
}

// Purpose: Execute a compiled test binary and collect its exit code.
// Inputs: path is the binary path to execute.
// Outputs: Returns true on success and fills exit_code.
// Invariants/Assumptions: Exit status matches main() return value.
static bool emu_exec_run_binary(const char* path, int* exit_code) {
  const char* argv[] = {path, NULL};
  return emu_exec_run_process(argv, false, exit_code);
}

// Purpose: Run one emulator execution test and compare against the host compiler.
// Inputs: test is the test descriptor to run.
// Outputs: Returns true if emulator and host results match.
// Invariants/Assumptions: The host compiler result matches main()'s return value.
static bool emu_exec_run_test(const struct EmuExecTest* test) {
  char out_path[kEmuExecMaxPath];
  int written = snprintf(out_path, sizeof(out_path), "%s/%s.gcc", kEmuExecOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(out_path)) {
    emu_exec_error(test->name, "path", "output path is too long");
    return false;
  }

  int gcc_exit = 0;
  if (!emu_exec_compile_with_host(test->path, out_path, &gcc_exit)) {
    if (gcc_exit >= 0) {
      emu_exec_error(test->name, "gcc", "host compiler exited with %d", gcc_exit);
    } else {
      emu_exec_error(test->name, "gcc", "host compiler failed to run");
    }
    return false;
  }

  int gcc_result = 0;
  if (!emu_exec_run_binary(out_path, &gcc_result)) {
    emu_exec_error(test->name, "run", "host binary failed to execute");
    return false;
  }

  char hex_path[kEmuExecMaxPath];
  written = snprintf(hex_path, sizeof(hex_path), "%s/%s.hex", kEmuExecOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(hex_path)) {
    emu_exec_error(test->name, "path", "hex output path is too long");
    return false;
  }

  int bcc_exit = 0;
  if (!emu_exec_compile_with_bcc(test->path, hex_path, &bcc_exit)) {
    if (bcc_exit >= 0) {
      emu_exec_error(test->name, "bcc", "compiler exited with %d", bcc_exit);
    } else {
      emu_exec_error(test->name, "bcc", "compiler failed to run");
    }
    return false;
  }

  int emu_result = 0;
  if (!emu_exec_run_emulator(hex_path, &emu_result)) {
    emu_exec_error(test->name, "emulator", "failed to run emulator");
    return false;
  }

  if (emu_result != gcc_result) {
    emu_exec_error(test->name, "compare", "emu result %d != gcc result %d", emu_result, gcc_result);
    return false;
  }

  return true;
}

int main(void) {
  if (!emu_exec_ensure_dir(kEmuExecBuildDir) || !emu_exec_ensure_dir(kEmuExecOutDir)) {
    fprintf(stderr, "Emu exec tests: failed to create %s\n", kEmuExecOutDir);
    return 1;
  }

  size_t total = sizeof(kEmuExecTests) / sizeof(kEmuExecTests[0]);
  size_t passed = 0;

  printf("Emulator execution results:\n");
  for (size_t i = 0; i < total; i++) {
    printf("- emu_exec_%s\n", kEmuExecTests[i].name);
    if (emu_exec_run_test(&kEmuExecTests[i])) {
      passed++;
    }
  }
  printf("Emulator execution results: %zu / %zu passed.\n", passed, total);

  if (passed == total) {
    printf("Emulator execution tests passed.\n");
    return 0;
  }

  printf("Emulator execution tests failed: %zu / %zu passed.\n", passed, total);
  return 1;
}
