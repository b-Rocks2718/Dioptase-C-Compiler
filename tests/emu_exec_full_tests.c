#include <ctype.h>
#include <dirent.h>
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

// Test code written by Codex

// Note: These tests intentionally rely on POSIX process and directory APIs plus
// a host C compiler to compare full emulator execution results against native execution.

// Design: For each C test in tests/exec, compile a host binary for reference output,
// then compile with bcc in kernel mode to assembly, assemble with kernel support
// sources into a hex image, and run the full emulator to compare results.

// Purpose: Configure test runner paths and sizing limits.
// Inputs/Outputs: Constants used by the full emulator execution tests.
// Invariants/Assumptions: Paths are relative to the compiler root.
static const char* kEmuExecFullBuildDir = "build";
static const char* kEmuExecFullOutDir = "build/emu_exec_full";
static const char* kEmuExecFullTestsDir = "tests/exec";
static const char* kEmuExecFullCompilerEnv = "TAC_TEST_CC";
static const char* kEmuExecFullBccEnv = "DIOPTASE_BCC";
static const char* kEmuExecFullAssemblerEnv = "DIOPTASE_ASSEMBLER";
static const char* kEmuExecFullEmulatorEnv = "DIOPTASE_EMULATOR_FULL";
static const char* kEmuExecFullEmuMaxCyclesEnv = "TAC_EMU_MAX_CYCLES";
static const char* kEmuExecFullCStandard = "-std=c11";
static const char* kEmuExecFullHostWarnFlag = "-w";
static const char* kEmuExecFullKernelArithmeticPath = "tests/kernel/arithmetic.s";
static const char* kEmuExecFullKernelInitPath = "tests/kernel/init.s";
static const size_t kEmuExecFullMaxPath = 512; // Supports test paths plus output suffixes.
static const size_t kEmuExecFullMaxOutput = 256; // Enough for emulator output + newline.
static const uint32_t kEmuExecFullEmuMaxCyclesDefault = 100000; // Bounds emulator runtime for stuck programs.
static const size_t kEmuExecFullTestListInitialCapacity = 8; // Handles small suites with minimal reallocs.
static const size_t kEmuExecFullTestListGrowthFactor = 2; // Doubling keeps append amortized constant time.
static const char kEmuExecFullTestSuffix[] = ".c";
static const size_t kEmuExecFullTestSuffixLen = sizeof(kEmuExecFullTestSuffix) - 1;

enum { kEmuExecFullDefaultBccPathCount = 2 };
static const char* const kEmuExecFullDefaultBccPaths[kEmuExecFullDefaultBccPathCount] = {
    "build/debug/bcc",
    "build/release/bcc",
};

enum { kEmuExecFullDefaultAssemblerPathCount = 2 };
static const char* const kEmuExecFullDefaultAssemblerPaths[kEmuExecFullDefaultAssemblerPathCount] = {
    "../../Dioptase-Assembler/build/debug/basm",
    "../../Dioptase-Assembler/build/release/basm",
};

enum { kEmuExecFullDefaultEmulatorPathCount = 2 };
static const char* const kEmuExecFullDefaultEmulatorPaths[kEmuExecFullDefaultEmulatorPathCount] = {
    "../../Dioptase-Emulators/Dioptase-Emulator-Full/target/debug/Dioptase-Emulator-Full",
    "../../Dioptase-Emulators/Dioptase-Emulator-Full/target/release/Dioptase-Emulator-Full",
};

// Purpose: Describe a full emulator execution test case.
// Inputs/Outputs: name identifies the test; path points to the C source.
// Invariants/Assumptions: path is a NUL-terminated file system path.
struct EmuExecFullTest {
  char* name;
  char* path;
};

// Purpose: Own a growable list of execution tests discovered on disk.
// Inputs/Outputs: tests holds owned strings; count/capacity track usage.
// Invariants/Assumptions: Each test name/path is heap allocated.
struct EmuExecFullTestList {
  struct EmuExecFullTest* tests;
  size_t count;
  size_t capacity;
};

// Purpose: Check whether a directory entry is "." or "..".
// Inputs: name is the directory entry name.
// Outputs: Returns true if the name is a dot entry.
// Invariants/Assumptions: name is a NUL-terminated string.
static bool emu_exec_full_is_dot_entry(const char* name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

// Purpose: Check whether a name ends with a given suffix.
// Inputs: name is the string to inspect; suffix is the expected suffix.
// Outputs: Returns true when suffix matches the tail of name.
// Invariants/Assumptions: suffix_len equals strlen(suffix).
static bool emu_exec_full_has_suffix(const char* name, const char* suffix, size_t suffix_len) {
  size_t name_len = strlen(name);
  if (name_len < suffix_len) {
    return false;
  }
  return memcmp(name + (name_len - suffix_len), suffix, suffix_len) == 0;
}

// Purpose: Ensure the test list has space for at least min_capacity entries.
// Inputs: list is the list to grow; min_capacity is the required capacity.
// Outputs: Returns true on success; list may be reallocated.
// Invariants/Assumptions: list is initialized to zeroed memory.
static bool emu_exec_full_ensure_test_capacity(struct EmuExecFullTestList* list, size_t min_capacity) {
  if (list->capacity >= min_capacity) {
    return true;
  }
  size_t new_capacity = list->capacity == 0 ? kEmuExecFullTestListInitialCapacity : list->capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > SIZE_MAX / kEmuExecFullTestListGrowthFactor) {
      fprintf(stderr, "Full emulator exec tests: test list size overflow\n");
      return false;
    }
    new_capacity *= kEmuExecFullTestListGrowthFactor;
  }

  struct EmuExecFullTest* resized =
      (struct EmuExecFullTest*)realloc(list->tests, new_capacity * sizeof(*resized));
  if (resized == NULL) {
    fprintf(stderr, "Full emulator exec tests: out of memory while listing %s\n",
            kEmuExecFullTestsDir);
    return false;
  }
  list->tests = resized;
  list->capacity = new_capacity;
  return true;
}

// Purpose: Append a test entry derived from a filename.
// Inputs: list stores the test entries; filename is the leaf name.
// Outputs: Returns true on success and appends to list.
// Invariants/Assumptions: filename ends with kEmuExecFullTestSuffix.
static bool emu_exec_full_append_test(struct EmuExecFullTestList* list, const char* filename) {
  size_t filename_len = strlen(filename);
  size_t name_len = filename_len - kEmuExecFullTestSuffixLen;
  size_t dir_len = strlen(kEmuExecFullTestsDir);
  size_t path_len = dir_len + 1 + filename_len;

  if (!emu_exec_full_ensure_test_capacity(list, list->count + 1)) {
    return false;
  }

  char* name = (char*)malloc(name_len + 1);
  if (name == NULL) {
    fprintf(stderr, "Full emulator exec tests: out of memory while naming %s\n", filename);
    return false;
  }
  memcpy(name, filename, name_len);
  name[name_len] = '\0';

  char* path = (char*)malloc(path_len + 1);
  if (path == NULL) {
    fprintf(stderr, "Full emulator exec tests: out of memory while building path for %s\n",
            filename);
    free(name);
    return false;
  }
  int written = snprintf(path, path_len + 1, "%s/%s", kEmuExecFullTestsDir, filename);
  if (written < 0 || (size_t)written != path_len) {
    fprintf(stderr, "Full emulator exec tests: failed to format path for %s\n", filename);
    free(path);
    free(name);
    return false;
  }

  struct stat st = {0};
  if (stat(path, &st) != 0) {
    fprintf(stderr, "Full emulator exec tests: unable to stat %s: %s\n", path, strerror(errno));
    free(path);
    free(name);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "Full emulator exec tests: %s is not a regular file\n", path);
    free(path);
    free(name);
    return false;
  }

  list->tests[list->count].name = name;
  list->tests[list->count].path = path;
  list->count++;
  return true;
}

// Purpose: Sort test entries by name for stable output.
// Inputs: lhs and rhs are EmuExecFullTest pointers cast from qsort.
// Outputs: Returns <0, 0, >0 like strcmp.
// Invariants/Assumptions: name fields are non-NULL.
static int emu_exec_full_compare_tests(const void* lhs, const void* rhs) {
  const struct EmuExecFullTest* left = (const struct EmuExecFullTest*)lhs;
  const struct EmuExecFullTest* right = (const struct EmuExecFullTest*)rhs;
  return strcmp(left->name, right->name);
}

// Purpose: Release memory allocated for a test list.
// Inputs: list is the list to free.
// Outputs: list is reset to an empty state.
// Invariants/Assumptions: list entries own their name/path strings.
static void emu_exec_full_free_test_list(struct EmuExecFullTestList* list) {
  if (list == NULL) {
    return;
  }
  for (size_t i = 0; i < list->count; i++) {
    free(list->tests[i].name);
    free(list->tests[i].path);
  }
  free(list->tests);
  list->tests = NULL;
  list->count = 0;
  list->capacity = 0;
}

// Purpose: Populate a test list from the tests/exec directory.
// Inputs: out_list receives the populated test list.
// Outputs: Returns true on success; out_list owns the entries.
// Invariants/Assumptions: tests/exec contains the desired .c sources.
static bool emu_exec_full_collect_tests(struct EmuExecFullTestList* out_list) {
  if (out_list == NULL) {
    return false;
  }
  *out_list = (struct EmuExecFullTestList){0};

  DIR* dir = opendir(kEmuExecFullTestsDir);
  if (dir == NULL) {
    fprintf(stderr, "Full emulator exec tests: unable to open %s: %s\n",
            kEmuExecFullTestsDir, strerror(errno));
    return false;
  }

  errno = 0;
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (emu_exec_full_is_dot_entry(entry->d_name)) {
      continue;
    }
    if (!emu_exec_full_has_suffix(entry->d_name,
                                  kEmuExecFullTestSuffix,
                                  kEmuExecFullTestSuffixLen)) {
      continue;
    }
    if (!emu_exec_full_append_test(out_list, entry->d_name)) {
      closedir(dir);
      emu_exec_full_free_test_list(out_list);
      return false;
    }
  }
  if (errno != 0) {
    fprintf(stderr, "Full emulator exec tests: failed while reading %s: %s\n",
            kEmuExecFullTestsDir, strerror(errno));
    closedir(dir);
    emu_exec_full_free_test_list(out_list);
    return false;
  }
  closedir(dir);

  if (out_list->count == 0) {
    fprintf(stderr, "Full emulator exec tests: no %s files found in %s\n",
            kEmuExecFullTestSuffix, kEmuExecFullTestsDir);
    emu_exec_full_free_test_list(out_list);
    return false;
  }

  qsort(out_list->tests, out_list->count, sizeof(out_list->tests[0]),
        emu_exec_full_compare_tests);
  return true;
}

// Purpose: Print a formatted failure message for a test stage.
// Inputs: test is the test name; stage is the pipeline stage; fmt is the detail.
// Outputs: Writes a diagnostic message to stderr.
// Invariants/Assumptions: fmt is a printf-style format string.
static void emu_exec_full_error(const char* test, const char* stage, const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "Full emulator exec test %s failed at %s: ", test, stage);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Purpose: Ensure the output directory exists.
// Inputs: path is the directory to create.
// Outputs: Returns true on success or if it already exists.
// Invariants/Assumptions: Uses POSIX mkdir semantics.
static bool emu_exec_full_ensure_dir(const char* path) {
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

// Purpose: Check whether a path points to an executable file.
// Inputs: path is the filesystem path to check.
// Outputs: Returns true when the file is executable.
// Invariants/Assumptions: Uses POSIX access().
static bool emu_exec_full_is_executable(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return false;
  }
  return access(path, X_OK) == 0;
}

// Purpose: Parse a non-zero u32 from a decimal string.
// Inputs: text is the string to parse.
// Outputs: Returns true on success and fills out_value.
// Invariants/Assumptions: Rejects values outside the uint32_t range or zero.
static bool emu_exec_full_parse_u32_nonzero(const char* text, uint32_t* out_value) {
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
static const char* emu_exec_full_select_tool(const char* env_name,
                                             const char* const* defaults,
                                             size_t default_count) {
  const char* env = getenv(env_name);
  if (env != NULL && env[0] != '\0') {
    if (emu_exec_full_is_executable(env)) {
      return env;
    }
    fprintf(stderr, "Full emulator exec tests: %s=%s is not executable\n", env_name, env);
  }
  for (size_t i = 0; i < default_count; i++) {
    if (emu_exec_full_is_executable(defaults[i])) {
      return defaults[i];
    }
  }
  return NULL;
}

// Purpose: Run a subprocess, forwarding stdio directly.
// Inputs: argv is a NULL-terminated command vector; search_path selects execvp.
// Outputs: Returns true on successful spawn; exit_code receives the exit status.
// Invariants/Assumptions: Uses POSIX fork/exec/wait.
static bool emu_exec_full_run_process(const char* const* argv, bool search_path, int* exit_code) {
  if (argv == NULL || argv[0] == NULL) {
    return false;
  }
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
    if (exit_code != NULL) {
      *exit_code = WEXITSTATUS(status);
    }
    return true;
  }
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "process %s terminated by signal %d\n", argv[0], WTERMSIG(status));
  }
  return false;
}

// Purpose: Run a subprocess and capture stdout to a buffer.
// Inputs: argv is the command vector; buffer stores captured output.
// Outputs: Returns true on success and fills exit_code and buffer.
// Invariants/Assumptions: buffer is writable and max_len > 0.
static bool emu_exec_full_run_process_capture(const char* const* argv,
                                              bool search_path,
                                              char* buffer,
                                              size_t max_len,
                                              int* exit_code) {
  if (argv == NULL || argv[0] == NULL || buffer == NULL || max_len == 0) {
    fprintf(stderr, "emu_exec_full_run_process_capture: invalid buffer\n");
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
  while (total + 1 < max_len) {
    ssize_t amount = read(pipe_fds[0], buffer + total, max_len - 1 - total);
    if (amount == 0) {
      break;
    }
    if (amount < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "read failed for %s: %s\n", argv[0], strerror(errno));
      close(pipe_fds[0]);
      return false;
    }
    total += (size_t)amount;
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
static bool emu_exec_full_compile_with_host(const char* source_path,
                                            const char* out_path,
                                            int* exit_code) {
  const char* compiler = getenv(kEmuExecFullCompilerEnv);
  if (compiler == NULL || compiler[0] == '\0') {
    compiler = "gcc";
  }
  const char* argv[] = {compiler, kEmuExecFullCStandard, kEmuExecFullHostWarnFlag,
                        source_path, "-o", out_path, NULL};
  int local_exit = 0;
  if (!emu_exec_full_run_process(argv, true, &local_exit)) {
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

// Purpose: Compile a test with the Dioptase compiler to a kernel-mode assembly file.
// Inputs: source_path is the test file; out_path is the output assembly path.
// Outputs: Returns true if the compile succeeds; exit_code receives the process status.
// Invariants/Assumptions: Uses DIOPTASE_BCC or local build paths.
static bool emu_exec_full_compile_with_bcc_to_asm(const char* source_path,
                                                  const char* out_path,
                                                  int* exit_code) {
  const char* compiler = emu_exec_full_select_tool(kEmuExecFullBccEnv,
                                                   kEmuExecFullDefaultBccPaths,
                                                   kEmuExecFullDefaultBccPathCount);
  if (compiler == NULL) {
    fprintf(stderr, "Full emulator exec tests: unable to find compiler binary (set %s)\n",
            kEmuExecFullBccEnv);
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  const char* argv[] = {compiler, "-s", "-kernel", "-o", out_path, source_path, NULL};
  int local_exit = 0;
  if (!emu_exec_full_run_process(argv, true, &local_exit)) {
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

// Purpose: Assemble a kernel-mode hex image from a test assembly file.
// Inputs: asm_path is the compiled test assembly; out_path is the output hex path.
// Outputs: Returns true if assembly succeeds; exit_code receives the process status.
// Invariants/Assumptions: Assembles with kernel init and arithmetic support sources.
// The init file must be first so its .origin establishes the kernel entry point.
static bool emu_exec_full_assemble_kernel_image(const char* asm_path,
                                                const char* out_path,
                                                int* exit_code) {
  const char* assembler = emu_exec_full_select_tool(kEmuExecFullAssemblerEnv,
                                                    kEmuExecFullDefaultAssemblerPaths,
                                                    kEmuExecFullDefaultAssemblerPathCount);
  if (assembler == NULL) {
    fprintf(stderr, "Full emulator exec tests: unable to find assembler binary (set %s)\n",
            kEmuExecFullAssemblerEnv);
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  const char* argv[] = {assembler, "-kernel", "-o", out_path,
                        kEmuExecFullKernelInitPath, asm_path,
                        kEmuExecFullKernelArithmeticPath, NULL};
  int local_exit = 0;
  if (!emu_exec_full_run_process(argv, true, &local_exit)) {
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
static bool emu_exec_full_parse_emulator_result(const char* text, int* out_value) {
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

// Purpose: Run the Dioptase full emulator on a hex image and capture its result.
// Inputs: hex_path is the hex file to execute.
// Outputs: Returns true on success and fills out_result.
// Invariants/Assumptions: Emulator prints the return value as a hex number.
static bool emu_exec_full_run_emulator(const char* hex_path, int* out_result) {
  const char* emulator = emu_exec_full_select_tool(kEmuExecFullEmulatorEnv,
                                                   kEmuExecFullDefaultEmulatorPaths,
                                                   kEmuExecFullDefaultEmulatorPathCount);
  if (emulator == NULL) {
    fprintf(stderr, "Full emulator exec tests: unable to find emulator binary (set %s)\n",
            kEmuExecFullEmulatorEnv);
    return false;
  }
  uint32_t max_cycles = kEmuExecFullEmuMaxCyclesDefault;
  const char* max_cycles_env = getenv(kEmuExecFullEmuMaxCyclesEnv);
  if (max_cycles_env != NULL && max_cycles_env[0] != '\0') {
    if (!emu_exec_full_parse_u32_nonzero(max_cycles_env, &max_cycles)) {
      fprintf(stderr, "Full emulator exec tests: invalid %s=%s (expected non-zero u32)\n",
              kEmuExecFullEmuMaxCyclesEnv, max_cycles_env);
      return false;
    }
  }

  char max_cycles_arg[32];
  snprintf(max_cycles_arg, sizeof(max_cycles_arg), "%u", max_cycles);
  const char* argv[] = {emulator, "--max-cycles", max_cycles_arg, hex_path, NULL};
  char buffer[kEmuExecFullMaxOutput];
  int exit_code = 0;
  if (!emu_exec_full_run_process_capture(argv, true, buffer, sizeof(buffer), &exit_code)) {
    return false;
  }
  if (exit_code != 0) {
    fprintf(stderr,
            "full emulator exited with status %d (max cycles %u; override via %s)\n",
            exit_code, max_cycles, kEmuExecFullEmuMaxCyclesEnv);
    return false;
  }
  if (!emu_exec_full_parse_emulator_result(buffer, out_result)) {
    fprintf(stderr, "unable to parse full emulator output: %s\n", buffer);
    return false;
  }
  return true;
}

// Purpose: Execute a compiled test binary and collect its exit code.
// Inputs: path is the binary path to execute.
// Outputs: Returns true on success and fills exit_code.
// Invariants/Assumptions: Exit status matches main() return value.
static bool emu_exec_full_run_binary(const char* path, int* exit_code) {
  const char* argv[] = {path, NULL};
  return emu_exec_full_run_process(argv, false, exit_code);
}

// Purpose: Run one full emulator execution test and compare against the host compiler.
// Inputs: test is the test descriptor to run.
// Outputs: Returns true if emulator and host results match.
// Invariants/Assumptions: The host compiler result matches main()'s return value.
static bool emu_exec_full_run_test(const struct EmuExecFullTest* test) {
  char out_path[kEmuExecFullMaxPath];
  int written = snprintf(out_path, sizeof(out_path), "%s/%s.gcc", kEmuExecFullOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(out_path)) {
    emu_exec_full_error(test->name, "path", "output path is too long");
    return false;
  }

  int gcc_exit = 0;
  if (!emu_exec_full_compile_with_host(test->path, out_path, &gcc_exit)) {
    if (gcc_exit >= 0) {
      emu_exec_full_error(test->name, "gcc", "host compiler exited with %d", gcc_exit);
    } else {
      emu_exec_full_error(test->name, "gcc", "host compiler failed to run");
    }
    return false;
  }

  int gcc_result = 0;
  if (!emu_exec_full_run_binary(out_path, &gcc_result)) {
    emu_exec_full_error(test->name, "run", "host binary failed to execute");
    return false;
  }

  char asm_path[kEmuExecFullMaxPath];
  written = snprintf(asm_path, sizeof(asm_path), "%s/%s.s", kEmuExecFullOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(asm_path)) {
    emu_exec_full_error(test->name, "path", "assembly output path is too long");
    return false;
  }

  int bcc_exit = 0;
  if (!emu_exec_full_compile_with_bcc_to_asm(test->path, asm_path, &bcc_exit)) {
    if (bcc_exit >= 0) {
      emu_exec_full_error(test->name, "bcc", "compiler exited with %d", bcc_exit);
    } else {
      emu_exec_full_error(test->name, "bcc", "compiler failed to run");
    }
    return false;
  }

  char hex_path[kEmuExecFullMaxPath];
  written = snprintf(hex_path, sizeof(hex_path), "%s/%s.hex", kEmuExecFullOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(hex_path)) {
    emu_exec_full_error(test->name, "path", "hex output path is too long");
    return false;
  }

  int asm_exit = 0;
  if (!emu_exec_full_assemble_kernel_image(asm_path, hex_path, &asm_exit)) {
    if (asm_exit >= 0) {
      emu_exec_full_error(test->name, "basm", "assembler exited with %d", asm_exit);
    } else {
      emu_exec_full_error(test->name, "basm", "assembler failed to run");
    }
    return false;
  }

  int emu_result = 0;
  if (!emu_exec_full_run_emulator(hex_path, &emu_result)) {
    emu_exec_full_error(test->name, "emulator", "failed to run emulator");
    return false;
  }

  if (emu_result != gcc_result) {
    emu_exec_full_error(test->name, "compare", "emu result %d != gcc result %d",
                        emu_result, gcc_result);
    return false;
  }

  return true;
}

int main(void) {
  if (!emu_exec_full_ensure_dir(kEmuExecFullBuildDir) ||
      !emu_exec_full_ensure_dir(kEmuExecFullOutDir)) {
    fprintf(stderr, "Full emulator exec tests: failed to create %s\n", kEmuExecFullOutDir);
    return 1;
  }

  struct EmuExecFullTestList tests = {0};
  if (!emu_exec_full_collect_tests(&tests)) {
    return 1;
  }

  size_t total = tests.count;
  size_t passed = 0;

  printf("Full emulator execution results:\n");
  for (size_t i = 0; i < total; i++) {
    printf("- emu_exec_full_%s\n", tests.tests[i].name);
    if (emu_exec_full_run_test(&tests.tests[i])) {
      passed++;
    }
  }
  printf("Full emulator execution results: %zu / %zu passed.\n", passed, total);

  if (passed == total) {
    printf("Full emulator execution tests passed.\n");
    emu_exec_full_free_test_list(&tests);
    return 0;
  }

  printf("Full emulator execution tests failed: %zu / %zu passed.\n", passed, total);
  emu_exec_full_free_test_list(&tests);
  return 1;
}
