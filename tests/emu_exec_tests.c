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
// a host C compiler to compare emulator execution results against native execution.

// Configure test runner paths and sizing limits.
static const char* kEmuExecBuildDir = "build";
static const char* kEmuExecOutDir = "build/emu_exec";
static const char* kEmuExecTestsDir = "tests/exec";
static const char* kEmuExecCompilerEnv = "TAC_TEST_CC";
static const char* kEmuExecBccEnv = "DIOPTASE_BCC";
static const char* kEmuExecEmulatorEnv = "DIOPTASE_EMULATOR_SIMPLE";
static const char* kEmuExecEmuMaxCyclesEnv = "TAC_EMU_MAX_CYCLES";
static const char* kEmuExecCStandard = "-std=c11";
static const char* kEmuExecHostWarnFlag = "-w";
static const size_t kEmuExecMaxPath = 512; // Supports test paths plus output suffixes.
static const size_t kEmuExecMaxOutput = 256; // Enough for emulator output + newline.
static const uint32_t kEmuExecEmuMaxCyclesDefault = 100000; // Bounds emulator runtime for stuck programs.
static const size_t kEmuExecTestListInitialCapacity = 8; // Handles small suites with minimal reallocs.
static const size_t kEmuExecTestListGrowthFactor = 2; // Doubling keeps append amortized constant time.
static const char kEmuExecTestSuffix[] = ".c";
static const size_t kEmuExecTestSuffixLen = sizeof(kEmuExecTestSuffix) - 1;
enum { kEmuExecBccFixedArgCount = 5 }; // Compiler, source, -o, output, and NULL.

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

// Describe an emulator execution test case.
// path is a NUL-terminated file system path.
struct EmuExecTest {
  char* name;
  char* path;
};

// Own a growable list of execution tests discovered on disk.
// tests holds owned strings; count/capacity track usage.
struct EmuExecTestList {
  struct EmuExecTest* tests;
  size_t count;
  size_t capacity;
};

// Restrict runner arguments to optimization switches accepted by bcc.
// Returns false and diagnoses the first unsupported option.
static bool emu_exec_validate_optimization_options(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (strcmp(arg, "-constant-fold") != 0 && strcmp(arg, "-dead-code") != 0 &&
        strcmp(arg, "-copy-prop") != 0 && strcmp(arg, "-dead-store") != 0 &&
        strcmp(arg, "-tail-call") != 0 && strcmp(arg, "-inline") != 0 &&
        strcmp(arg, "-peephole") != 0 && strcmp(arg, "-reg-alloc") != 0 &&
        strcmp(arg, "-opt") != 0) {
      fprintf(stderr, "Emu exec tests: unsupported optimization flag '%s'\n", arg);
      return false;
    }
  }
  return true;
}

// Check whether a directory entry is "." or "..".
// Returns true if the name is a dot entry.
// name is a NUL-terminated string.
static bool emu_exec_is_dot_entry(const char* name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

// Check whether a name ends with a given suffix.
// Returns true when suffix matches the tail of name.
static bool emu_exec_has_suffix(const char* name, const char* suffix, size_t suffix_len) {
  size_t name_len = strlen(name);
  if (name_len < suffix_len) {
    return false;
  }
  return memcmp(name + (name_len - suffix_len), suffix, suffix_len) == 0;
}

// Ensure the test list has space for at least min_capacity entries.
// list is the list to grow; min_capacity is the required capacity.
// Returns true on success; list may be reallocated.
static bool emu_exec_ensure_test_capacity(struct EmuExecTestList* list, size_t min_capacity) {
  if (list->capacity >= min_capacity) {
    return true;
  }
  size_t new_capacity = list->capacity == 0 ? kEmuExecTestListInitialCapacity : list->capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > SIZE_MAX / kEmuExecTestListGrowthFactor) {
      fprintf(stderr, "Emu exec tests: test list size overflow\n");
      return false;
    }
    new_capacity *= kEmuExecTestListGrowthFactor;
  }

  struct EmuExecTest* resized =
      (struct EmuExecTest*)realloc(list->tests, new_capacity * sizeof(*resized));
  if (resized == NULL) {
    fprintf(stderr, "Emu exec tests: out of memory while listing %s\n", kEmuExecTestsDir);
    return false;
  }
  list->tests = resized;
  list->capacity = new_capacity;
  return true;
}

// Append a test entry derived from a filename.
// Returns true on success and appends to list.
static bool emu_exec_append_test(struct EmuExecTestList* list, const char* filename) {
  size_t filename_len = strlen(filename);
  size_t name_len = filename_len - kEmuExecTestSuffixLen;
  size_t dir_len = strlen(kEmuExecTestsDir);
  size_t path_len = dir_len + 1 + filename_len;

  if (!emu_exec_ensure_test_capacity(list, list->count + 1)) {
    return false;
  }

  char* name = (char*)malloc(name_len + 1);
  if (name == NULL) {
    fprintf(stderr, "Emu exec tests: out of memory while naming %s\n", filename);
    return false;
  }
  memcpy(name, filename, name_len);
  name[name_len] = '\0';

  char* path = (char*)malloc(path_len + 1);
  if (path == NULL) {
    fprintf(stderr, "Emu exec tests: out of memory while building path for %s\n", filename);
    free(name);
    return false;
  }
  int written = snprintf(path, path_len + 1, "%s/%s", kEmuExecTestsDir, filename);
  if (written < 0 || (size_t)written != path_len) {
    fprintf(stderr, "Emu exec tests: failed to format path for %s\n", filename);
    free(path);
    free(name);
    return false;
  }

  struct stat st = {0};
  if (stat(path, &st) != 0) {
    fprintf(stderr, "Emu exec tests: unable to stat %s: %s\n", path, strerror(errno));
    free(path);
    free(name);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "Emu exec tests: %s is not a regular file\n", path);
    free(path);
    free(name);
    return false;
  }

  list->tests[list->count].name = name;
  list->tests[list->count].path = path;
  list->count++;
  return true;
}

// Sort test entries by name for stable output.
// Returns <0, 0, >0 like strcmp.
// name fields are non-NULL.
static int emu_exec_compare_tests(const void* lhs, const void* rhs) {
  const struct EmuExecTest* left = (const struct EmuExecTest*)lhs;
  const struct EmuExecTest* right = (const struct EmuExecTest*)rhs;
  return strcmp(left->name, right->name);
}

// Release memory allocated for a test list.
// list is reset to an empty state.
static void emu_exec_free_test_list(struct EmuExecTestList* list) {
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

// Populate a test list from the tests/exec directory.
// Returns true on success; out_list owns the entries.
static bool emu_exec_collect_tests(struct EmuExecTestList* out_list) {
  if (out_list == NULL) {
    return false;
  }
  *out_list = (struct EmuExecTestList){0};

  DIR* dir = opendir(kEmuExecTestsDir);
  if (dir == NULL) {
    fprintf(stderr, "Emu exec tests: unable to open %s: %s\n",
            kEmuExecTestsDir, strerror(errno));
    return false;
  }

  errno = 0;
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (emu_exec_is_dot_entry(entry->d_name)) {
      continue;
    }
    if (!emu_exec_has_suffix(entry->d_name, kEmuExecTestSuffix, kEmuExecTestSuffixLen)) {
      continue;
    }
    if (!emu_exec_append_test(out_list, entry->d_name)) {
      closedir(dir);
      emu_exec_free_test_list(out_list);
      return false;
    }
  }
  if (errno != 0) {
    fprintf(stderr, "Emu exec tests: failed while reading %s: %s\n",
            kEmuExecTestsDir, strerror(errno));
    closedir(dir);
    emu_exec_free_test_list(out_list);
    return false;
  }
  closedir(dir);

  if (out_list->count == 0) {
    fprintf(stderr, "Emu exec tests: no %s files found in %s\n",
            kEmuExecTestSuffix, kEmuExecTestsDir);
    emu_exec_free_test_list(out_list);
    return false;
  }

  qsort(out_list->tests, out_list->count, sizeof(out_list->tests[0]),
        emu_exec_compare_tests);
  return true;
}

// Print a formatted failure message for a test stage.
// Writes a diagnostic message to stderr.
static void emu_exec_error(const char* test, const char* stage, const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "Emu exec test %s failed at %s: ", test, stage);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Ensure the output directory exists.
// Returns true on success or if it already exists.
static bool emu_exec_ensure_dir(const char* path) {
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

// Check whether a path points to an executable file.
// Returns true when the file is executable.
static bool emu_exec_is_executable(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return false;
  }
  return access(path, X_OK) == 0;
}

// Parse a non-zero u32 from a decimal string.
// Returns true on success and fills out_value.
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

// Select a tool path from an environment override or defaults.
// Returns a usable tool path or NULL if none are available.
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

// Spawn a child process and capture its exit code.
// Returns true on success and fills exit_code.
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

// Spawn a child process and capture stdout output.
// Returns true on success, fills exit_code, and writes output to buffer.
// buffer_size must be > 0; output is NUL-terminated.
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

// Compile a test with the host compiler for reference output.
// Returns true if the compile succeeds; exit_code receives the process status.
static bool emu_exec_compile_with_host(const char* source_path,
                                       const char* out_path,
                                       int* exit_code) {
  const char* compiler = getenv(kEmuExecCompilerEnv);
  if (compiler == NULL || compiler[0] == '\0') {
    compiler = "gcc";
  }
  const char* argv[] = {compiler, kEmuExecCStandard, kEmuExecHostWarnFlag,
                        source_path, "-o", out_path, NULL};
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

// Compile a test with the Dioptase compiler to a hex image.
// Returns true if the compile succeeds; exit_code receives the process status.
static bool emu_exec_compile_with_bcc(const char* source_path,
                                      const char* out_path,
                                      int compiler_option_count,
                                      char* const* compiler_options,
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
  size_t argv_count = (size_t)compiler_option_count + kEmuExecBccFixedArgCount;
  const char** bcc_argv = (const char**)malloc(argv_count * sizeof(*bcc_argv));
  if (bcc_argv == NULL) {
    fprintf(stderr, "Emu exec tests: out of memory while preparing bcc for %s\n", source_path);
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  size_t arg_index = 0;
  bcc_argv[arg_index++] = compiler;
  for (int i = 0; i < compiler_option_count; i++) {
    bcc_argv[arg_index++] = compiler_options[i];
  }
  bcc_argv[arg_index++] = source_path;
  bcc_argv[arg_index++] = "-o";
  bcc_argv[arg_index++] = out_path;
  bcc_argv[arg_index] = NULL;
  int local_exit = 0;
  if (!emu_exec_run_process(bcc_argv, true, &local_exit)) {
    free(bcc_argv);
    if (exit_code != NULL) {
      *exit_code = -1;
    }
    return false;
  }
  free(bcc_argv);
  if (exit_code != NULL) {
    *exit_code = local_exit;
  }
  return local_exit == 0;
}

// Parse a hex string result from the emulator into a signed int.
// Returns true on success and fills out_value.
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

// Run the Dioptase emulator on a hex image and capture its result.
// Returns true on success and fills out_result.
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

// Execute a compiled test binary and collect its exit code.
// Returns true on success and fills exit_code.
static bool emu_exec_run_binary(const char* path, int* exit_code) {
  const char* argv[] = {path, NULL};
  return emu_exec_run_process(argv, false, exit_code);
}

// Run one emulator execution test and compare against the host compiler.
// Returns true if emulator and host results match.
static bool emu_exec_run_test(const struct EmuExecTest* test,
                              int compiler_option_count,
                              char* const* compiler_options) {
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
  if (!emu_exec_compile_with_bcc(test->path, hex_path, compiler_option_count,
                                  compiler_options, &bcc_exit)) {
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

int main(int argc, char** argv) { /* Exercise emu exec tests behavior. */
  if (!emu_exec_validate_optimization_options(argc, argv)) {
    return 1;
  }
  int compiler_option_count = argc - 1;
  char* const* compiler_options = argv + 1;

  if (!emu_exec_ensure_dir(kEmuExecBuildDir) || !emu_exec_ensure_dir(kEmuExecOutDir)) {
    fprintf(stderr, "Emu exec tests: failed to create %s\n", kEmuExecOutDir);
    return 1;
  }

  struct EmuExecTestList tests = {0};
  if (!emu_exec_collect_tests(&tests)) {
    return 1;
  }

  size_t total = tests.count;
  size_t passed = 0;

  printf("Emulator execution results%s:\n",
         compiler_option_count > 0 ? " (optimized)" : "");
  for (size_t i = 0; i < total; i++) {
    printf("- emu_exec_%s\n", tests.tests[i].name);
    if (emu_exec_run_test(&tests.tests[i], compiler_option_count, compiler_options)) {
      passed++;
    }
  }
  printf("Emulator execution results: %zu / %zu passed.\n", passed, total);

  if (passed == total) {
    printf("Emulator execution tests passed.\n");
    emu_exec_free_test_list(&tests);
    return 0;
  }

  printf("Emulator execution tests failed: %zu / %zu passed.\n", passed, total);
  emu_exec_free_test_list(&tests);
  return 1;
}
