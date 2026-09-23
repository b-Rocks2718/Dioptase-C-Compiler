#include "TAC.h"
#include "arena.h"
#include "identifier_resolution.h"
#include "label_resolution.h"
#include "lexer.h"
#include "optimization.h"
#include "parser.h"
#include "preprocessor.h"
#include "source_location.h"
#include "token_array.h"
#include "typechecking.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Compare C fixtures executed natively and through the TAC interpreter.

// Note: These tests intentionally rely on POSIX process and directory APIs plus
// a host C compiler to compare TAC execution results against native execution.

// Configure test runner paths and sizing limits.
static const char* kTacExecBuildDir = "build";
static const char* kTacExecOutDir = "build/tac_exec";
static const char* kTacExecTestsDir = "tests/exec";
static const char* kTacExecCompilerEnv = "TAC_TEST_CC";
static const char* kTacExecCStandard = "-std=c11";
static const char* kTacExecHostWarnFlag = "-w";
static const size_t kTacExecMaxPath = 512; // Supports test paths plus output suffixes.
static const size_t kTacExecArenaBlockSize = 16384; // Mirrors main.c arena sizing.
static const size_t kTacExecTestListInitialCapacity = 8; // Handles small suites with minimal reallocs.
static const size_t kTacExecTestListGrowthFactor = 2; // Doubling keeps append amortized constant time.
static const char kTacExecTestSuffix[] = ".c";
static const size_t kTacExecTestSuffixLen = sizeof(kTacExecTestSuffix) - 1;
// Fixtures in tests/exec containing this text are skipped here but still run by
// the emulator suites, e.g. ones that pass structs by value, which the TAC
// interpreter does not support.
static const char kTacExecSkipMarker[] = "tac-exec: skip";
static const int kTacExecChildPassExitCode = 0;
static const int kTacExecChildFailExitCode = 1;

// Describe a TAC execution test case.
// path is a NUL-terminated file system path.
struct TacExecTest {
  char* name;
  char* path;
};

// Own a growable list of execution tests discovered on disk.
// tests holds owned strings; count/capacity track usage.
struct TacExecTestList {
  struct TacExecTest* tests;
  size_t count;
  size_t capacity;
};

// Parse the same optimization switches accepted by bcc.
// Returns false and prints an actionable diagnostic for any non-optimization flag.
static bool tac_exec_parse_optimization_options(int argc,
                                                char** argv,
                                                struct OptimizationOptions* options) {
  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (strcmp(arg, "-constant-fold") == 0) {
      options->constant_fold = true;
    } else if (strcmp(arg, "-dead-code") == 0) {
      options->dead_code_elim = true;
    } else if (strcmp(arg, "-copy-prop") == 0) {
      options->copy_prop = true;
    } else if (strcmp(arg, "-dead-store") == 0) {
      options->dead_store_elim = true;
    } else if (strcmp(arg, "-tail-call") == 0) {
      options->tail_call_opt = true;
    } else if (strcmp(arg, "-inline") == 0) {
      options->inline_opt = true;
    } else if (strcmp(arg, "-peephole") == 0) {
      options->peephole_opt = true;
    } else if (strcmp(arg, "-reg-alloc") == 0) {
      options->reg_alloc = true;
    } else if (strcmp(arg, "-opt") == 0) {
      options->constant_fold = true;
      options->dead_code_elim = true;
      options->copy_prop = true;
      options->dead_store_elim = true;
      options->tail_call_opt = true;
      options->inline_opt = true;
      options->peephole_opt = true;
      options->reg_alloc = true;
    } else {
      fprintf(stderr, "TAC exec tests: unsupported optimization flag '%s'\n", arg);
      return false;
    }
  }
  return true;
}

// Check whether a directory entry is "." or "..".
// Returns true if the name is a dot entry.
// name is a NUL-terminated string.
static bool tac_exec_is_dot_entry(const char* name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

// Check whether a name ends with a given suffix.
// Returns true when suffix matches the tail of name.
static bool tac_exec_has_suffix(const char* name, const char* suffix, size_t suffix_len) {
  size_t name_len = strlen(name);
  if (name_len < suffix_len) {
    return false;
  }
  return memcmp(name + (name_len - suffix_len), suffix, suffix_len) == 0;
}

// Ensure the test list has space for at least min_capacity entries.
// list is the list to grow; min_capacity is the required capacity.
// Returns true on success; list may be reallocated.
static bool tac_exec_ensure_test_capacity(struct TacExecTestList* list, size_t min_capacity) {
  if (list->capacity >= min_capacity) {
    return true;
  }
  size_t new_capacity = list->capacity == 0 ? kTacExecTestListInitialCapacity : list->capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > SIZE_MAX / kTacExecTestListGrowthFactor) {
      fprintf(stderr, "TAC exec tests: test list size overflow\n");
      return false;
    }
    new_capacity *= kTacExecTestListGrowthFactor;
  }

  struct TacExecTest* resized =
      (struct TacExecTest*)realloc(list->tests, new_capacity * sizeof(*resized));
  if (resized == NULL) {
    fprintf(stderr, "TAC exec tests: out of memory while listing %s\n", kTacExecTestsDir);
    return false;
  }
  list->tests = resized;
  list->capacity = new_capacity;
  return true;
}

// Append a test entry derived from a filename.
// Returns true on success and appends to list.
static bool tac_exec_append_test(struct TacExecTestList* list, const char* filename) {
  size_t filename_len = strlen(filename);
  size_t name_len = filename_len - kTacExecTestSuffixLen;
  size_t dir_len = strlen(kTacExecTestsDir);
  size_t path_len = dir_len + 1 + filename_len;

  if (!tac_exec_ensure_test_capacity(list, list->count + 1)) {
    return false;
  }

  char* name = (char*)malloc(name_len + 1);
  if (name == NULL) {
    fprintf(stderr, "TAC exec tests: out of memory while naming %s\n", filename);
    return false;
  }
  memcpy(name, filename, name_len);
  name[name_len] = '\0';

  char* path = (char*)malloc(path_len + 1);
  if (path == NULL) {
    fprintf(stderr, "TAC exec tests: out of memory while building path for %s\n", filename);
    free(name);
    return false;
  }
  int written = snprintf(path, path_len + 1, "%s/%s", kTacExecTestsDir, filename);
  if (written < 0 || (size_t)written != path_len) {
    fprintf(stderr, "TAC exec tests: failed to format path for %s\n", filename);
    free(path);
    free(name);
    return false;
  }

  struct stat st = {0};
  if (stat(path, &st) != 0) {
    fprintf(stderr, "TAC exec tests: unable to stat %s: %s\n", path, strerror(errno));
    free(path);
    free(name);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "TAC exec tests: %s is not a regular file\n", path);
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
static int tac_exec_compare_tests(const void* lhs, const void* rhs) {
  const struct TacExecTest* left = (const struct TacExecTest*)lhs;
  const struct TacExecTest* right = (const struct TacExecTest*)rhs;
  return strcmp(left->name, right->name);
}

// Release memory allocated for a test list.
// list is reset to an empty state.
static void tac_exec_free_test_list(struct TacExecTestList* list) {
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
static bool tac_exec_collect_tests(struct TacExecTestList* out_list) {
  if (out_list == NULL) {
    return false;
  }
  *out_list = (struct TacExecTestList){0};

  DIR* dir = opendir(kTacExecTestsDir);
  if (dir == NULL) {
    fprintf(stderr, "TAC exec tests: unable to open %s: %s\n",
            kTacExecTestsDir, strerror(errno));
    return false;
  }

  errno = 0;
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (tac_exec_is_dot_entry(entry->d_name)) {
      continue;
    }
    if (!tac_exec_has_suffix(entry->d_name, kTacExecTestSuffix, kTacExecTestSuffixLen)) {
      continue;
    }
    if (!tac_exec_append_test(out_list, entry->d_name)) {
      closedir(dir);
      tac_exec_free_test_list(out_list);
      return false;
    }
  }
  if (errno != 0) {
    fprintf(stderr, "TAC exec tests: failed while reading %s: %s\n",
            kTacExecTestsDir, strerror(errno));
    closedir(dir);
    tac_exec_free_test_list(out_list);
    return false;
  }
  closedir(dir);

  if (out_list->count == 0) {
    fprintf(stderr, "TAC exec tests: no %s files found in %s\n",
            kTacExecTestSuffix, kTacExecTestsDir);
    tac_exec_free_test_list(out_list);
    return false;
  }

  qsort(out_list->tests, out_list->count, sizeof(out_list->tests[0]),
        tac_exec_compare_tests);
  return true;
}

// Print a formatted failure message for a test stage.
// Writes a diagnostic message to stderr.
static void tac_exec_error(const char* test, const char* stage, const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "TAC exec test %s failed at %s: ", test, stage);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Read a file into a newly allocated, NUL-terminated buffer.
// Returns true on success and fills out_text.
static bool tac_exec_read_file(const char* path, char** out_text) {
  FILE* fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return false;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }
  char* buffer = (char*)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fclose(fp);
    return false;
  }
  size_t read_count = fread(buffer, 1, (size_t)size, fp);
  fclose(fp);
  if (read_count != (size_t)size) {
    free(buffer);
    return false;
  }
  buffer[size] = '\0';
  *out_text = buffer;
  return true;
}

// Check whether a fixture opts out of TAC execution via kTacExecSkipMarker.
// Returns false if the fixture cannot be read.
static bool tac_exec_is_skipped(const struct TacExecTest* test, bool* out_skipped) {
  char* source = NULL;
  if (!tac_exec_read_file(test->path, &source)) {
    tac_exec_error(test->name, "read", "unable to read %s: %s", test->path, strerror(errno));
    return false;
  }
  *out_skipped = strstr(source, kTacExecSkipMarker) != NULL;
  free(source);
  return true;
}

// Ensure a directory exists for build outputs.
// Returns true on success or if it already exists.
static bool tac_exec_ensure_dir(const char* path) {
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

// Spawn a child process and capture its exit code.
// Returns true on success and fills exit_code.
static bool tac_exec_run_process(const char* const* argv, bool search_path, int* exit_code) {
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

// Compile a test with the host compiler for reference output.
// Returns true if the compile succeeds; exit_code receives the process status.
static bool tac_exec_compile_with_host(const char* source_path,
                                       const char* out_path,
                                       int* exit_code) {
  const char* compiler = getenv(kTacExecCompilerEnv);
  if (compiler == NULL || compiler[0] == '\0') {
    compiler = "gcc";
  }
  const char* argv[] = {compiler, kTacExecCStandard, kTacExecHostWarnFlag,
                        source_path, "-o", out_path, NULL};
  int local_exit = 0;
  if (!tac_exec_run_process(argv, true, &local_exit)) {
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

// Execute a compiled test binary and collect its exit code.
// Returns true on success and fills exit_code.
static bool tac_exec_run_binary(const char* path, int* exit_code) {
  const char* argv[] = {path, NULL};
  return tac_exec_run_process(argv, false, exit_code);
}

// Collect static variable TAC entries for a program.
// global_symbol_table is valid until arena_destroy().
static void tac_exec_collect_statics(struct TACProg* prog) {
  if (prog == NULL || global_symbol_table == NULL) {
    return;
  }
  struct TopLevel* statics_head = NULL;
  struct TopLevel* statics_tail = NULL;
  for (size_t i = 0; i < global_symbol_table->size; i++) {
    for (struct SymbolEntry* entry = global_symbol_table->arr[i];
         entry != NULL;
         entry = entry->next) {
      struct TopLevel* top_level = symbol_to_TAC(entry);
      if (top_level != NULL) {
        if (statics_head == NULL) {
          statics_head = top_level;
          statics_tail = top_level;
        } else {
          statics_tail->next = top_level;
          statics_tail = top_level;
        }
      }
    }
  }
  prog->statics = statics_head;
}

// Build TAC from a source file and interpret it.
// Returns true on success and fills out_result.
static bool tac_exec_run_tac(const struct TacExecTest* test,
                             struct OptimizationOptions optimization_options,
                             int* out_result) {
  bool ok = false;
  char* source = NULL;
  struct PreprocessResult preprocessed = {0};
  struct TokenArray* tokens = NULL;

  if (!tac_exec_read_file(test->path, &source)) {
    tac_exec_error(test->name, "read", "unable to read %s: %s", test->path, strerror(errno));
    goto cleanup;
  }

  if (!preprocess(source, test->path, 0, NULL, &preprocessed)) {
    tac_exec_error(test->name, "preprocess", "preprocessor reported an error");
    goto cleanup;
  }
  set_source_context_with_map(test->path, preprocessed.text, &preprocessed.map);

  tokens = lex(preprocessed.text);
  if (tokens == NULL) {
    tac_exec_error(test->name, "lex", "lexer returned NULL");
    goto cleanup;
  }

  arena_init(kTacExecArenaBlockSize);
  struct Program* prog = parse_prog(tokens);
  if (prog == NULL) {
    tac_exec_error(test->name, "parse", "parser returned NULL");
    goto cleanup;
  }
  if (!resolve_prog(prog)) {
    tac_exec_error(test->name, "idents", "identifier resolution failed");
    goto cleanup;
  }
  if (!label_loops(prog)) {
    tac_exec_error(test->name, "labels", "label resolution failed");
    goto cleanup;
  }
  if (!typecheck_program(prog)) {
    tac_exec_error(test->name, "types", "typechecking failed");
    goto cleanup;
  }

  struct TACProg* tac_prog = prog_to_TAC(prog, false, optimization_options.tail_call_opt);
  if (tac_prog == NULL) {
    tac_exec_error(test->name, "tac", "TAC lowering failed");
    goto cleanup;
  }
  optimize(tac_prog, optimization_options);
  tac_exec_collect_statics(tac_prog);

  *out_result = tac_interpret_prog(tac_prog);
  ok = true;

cleanup:
  free(source);
  if (tokens != NULL) {
    destroy_token_array(tokens);
  }
  destroy_preprocess_result(&preprocessed);
  arena_destroy();
  global_symbol_table = NULL;
  return ok;
}

// Run one TAC execution test and compare against the host compiler.
// Returns true if TAC and host results match.
static bool tac_exec_run_test_internal(const struct TacExecTest* test,
                                       struct OptimizationOptions optimization_options) {
  int tac_result = 0;
  if (!tac_exec_run_tac(test, optimization_options, &tac_result)) {
    return false;
  }

  char out_path[kTacExecMaxPath];
  int written = snprintf(out_path, sizeof(out_path), "%s/%s.gcc", kTacExecOutDir, test->name);
  if (written < 0 || (size_t)written >= sizeof(out_path)) {
    tac_exec_error(test->name, "path", "output path is too long");
    return false;
  }

  int gcc_exit = 0;
  if (!tac_exec_compile_with_host(test->path, out_path, &gcc_exit)) {
    if (gcc_exit >= 0) {
      tac_exec_error(test->name, "gcc", "host compiler exited with %d", gcc_exit);
    } else {
      tac_exec_error(test->name, "gcc", "host compiler failed to run");
    }
    return false;
  }

  int gcc_result = 0;
  if (!tac_exec_run_binary(out_path, &gcc_result)) {
    tac_exec_error(test->name, "run", "host binary failed to execute");
    return false;
  }

  if (tac_result != gcc_result) {
    tac_exec_error(test->name, "compare", "TAC result %d != gcc result %d", tac_result, gcc_result);
    return false;
  }

  return true;
}

// Run one TAC execution test in a child process to isolate fatal errors.
// Returns true if the child process reports a passing result.
static bool tac_exec_run_test(const struct TacExecTest* test,
                              struct OptimizationOptions optimization_options,
                              struct TacExecTestList* inherited_tests) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    tac_exec_error(test->name, "fork", "fork failed: %s", strerror(errno));
    return false;
  }
  if (pid == 0) {
    bool ok = tac_exec_run_test_internal(test, optimization_options);
    // The forked child owns a copy of the parent's discovered-test list. Release
    // that copy before _exit so leak checking sees the child's true live state.
    tac_exec_free_test_list(inherited_tests);
    fflush(NULL);
    _exit(ok ? kTacExecChildPassExitCode : kTacExecChildFailExitCode);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    tac_exec_error(test->name, "waitpid", "waitpid failed: %s", strerror(errno));
    return false;
  }
  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == kTacExecChildPassExitCode) {
      return true;
    }
    if (exit_code != kTacExecChildFailExitCode) {
      tac_exec_error(test->name, "process", "exited with status %d", exit_code);
    }
    return false;
  }
  if (WIFSIGNALED(status)) {
    tac_exec_error(test->name, "process", "terminated by signal %d", WTERMSIG(status));
  } else {
    tac_exec_error(test->name, "process", "terminated unexpectedly");
  }
  return false;
}

int main(int argc, char** argv) { /* Exercise tac exec tests behavior. */
  struct OptimizationOptions optimization_options = {false};
  if (!tac_exec_parse_optimization_options(argc, argv, &optimization_options)) {
    return 1;
  }
  bool optimizations_enabled = argc > 1;

  if (!tac_exec_ensure_dir(kTacExecBuildDir) || !tac_exec_ensure_dir(kTacExecOutDir)) {
    fprintf(stderr, "TAC exec tests: failed to create %s\n", kTacExecOutDir);
    return 1;
  }

  struct TacExecTestList tests = {0};
  if (!tac_exec_collect_tests(&tests)) {
    return 1;
  }

  size_t total = 0;
  size_t passed = 0;
  size_t skipped = 0;
  printf("TAC execution results%s:\n", optimizations_enabled ? " (optimized)" : "");
  for (size_t i = 0; i < tests.count; i++) {
    bool skip = false;
    if (tac_exec_is_skipped(&tests.tests[i], &skip) && skip) {
      printf("- tac_exec_%s (skipped: contains \"%s\")\n", tests.tests[i].name, kTacExecSkipMarker);
      skipped++;
      continue;
    }
    total++;
    printf("- tac_exec_%s\n", tests.tests[i].name);
    if (tac_exec_run_test(&tests.tests[i], optimization_options, &tests)) {
      passed++;
    }
  }
  printf("TAC execution results: %zu / %zu passed (%zu skipped).\n", passed, total, skipped);

  if (passed == total) {
    printf("TAC execution tests passed.\n");
    tac_exec_free_test_list(&tests);
    return 0;
  }

  printf("TAC execution tests failed: %zu / %zu passed.\n", passed, total);
  tac_exec_free_test_list(&tests);
  return 1;
}
