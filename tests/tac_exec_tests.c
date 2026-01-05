#include "TAC.h"
#include "arena.h"
#include "identifier_resolution.h"
#include "label_resolution.h"
#include "lexer.h"
#include "parser.h"
#include "preprocessor.h"
#include "source_location.h"
#include "token_array.h"
#include "typechecking.h"

#include <errno.h>
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
// to compare TAC execution results against native execution.

// Purpose: Configure test runner paths and sizing limits.
// Inputs/Outputs: Constants used by the TAC execution tests.
// Invariants/Assumptions: Paths are relative to the compiler root.
static const char* kTacExecBuildDir = "build";
static const char* kTacExecOutDir = "build/tac_exec";
static const char* kTacExecCompilerEnv = "TAC_TEST_CC";
static const char* kTacExecCStandard = "-std=c11";
static const size_t kTacExecMaxPath = 512; // Supports test paths plus output suffixes.
static const size_t kTacExecArenaBlockSize = 16384; // Mirrors main.c arena sizing.

// Purpose: Describe a TAC execution test case.
// Inputs/Outputs: name identifies the test; path points to the C source.
// Invariants/Assumptions: path is a NUL-terminated file system path.
struct TacExecTest {
  const char* name;
  const char* path;
};

static const struct TacExecTest kTacExecTests[] = {
    {"arithmetic", "tests/tac_exec/arithmetic.c"},
    {"logical", "tests/tac_exec/logical.c"},
    {"loops", "tests/tac_exec/loops.c"},
    {"switch", "tests/tac_exec/switch.c"},
    {"goto", "tests/tac_exec/goto.c"},
    {"functions", "tests/tac_exec/functions.c"},
    {"pointers", "tests/tac_exec/pointers.c"},
    {"pointer_store", "tests/tac_exec/pointer_store.c"},
    {"global_pointer_basic", "tests/tac_exec/global_pointer_basic.c"},
    {"global_pointer_cross", "tests/tac_exec/global_pointer_cross.c"},
    {"globals", "tests/tac_exec/globals.c"},
    {"conditional", "tests/tac_exec/conditional.c"},
};

// Purpose: Print a formatted failure message for a test stage.
// Inputs: test is the test name; stage is the pipeline stage; fmt is the detail.
// Outputs: Writes a diagnostic message to stderr.
// Invariants/Assumptions: fmt is a printf-style format string.
static void tac_exec_error(const char* test, const char* stage, const char* fmt, ...) {
  va_list args;
  fprintf(stderr, "TAC exec test %s failed at %s: ", test, stage);
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

// Purpose: Read a file into a newly allocated, NUL-terminated buffer.
// Inputs: path is the file path to read.
// Outputs: Returns true on success and fills out_text.
// Invariants/Assumptions: Caller frees *out_text with free().
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

// Purpose: Ensure a directory exists for build outputs.
// Inputs: path is the directory to create.
// Outputs: Returns true on success or if it already exists.
// Invariants/Assumptions: Uses POSIX mkdir semantics.
static bool tac_exec_ensure_dir(const char* path) {
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST;
}

// Purpose: Spawn a child process and capture its exit code.
// Inputs: argv is the command array; search_path selects execvp vs execv.
// Outputs: Returns true on success and fills exit_code.
// Invariants/Assumptions: Uses POSIX fork/exec/wait APIs.
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

// Purpose: Compile a test with the host compiler for reference output.
// Inputs: source_path is the test file; out_path is the output binary.
// Outputs: Returns true if the compile succeeds; exit_code receives the process status.
// Invariants/Assumptions: Uses gcc/cc via PATH to match host behavior.
static bool tac_exec_compile_with_host(const char* source_path,
                                       const char* out_path,
                                       int* exit_code) {
  const char* compiler = getenv(kTacExecCompilerEnv);
  if (compiler == NULL || compiler[0] == '\0') {
    compiler = "gcc";
  }
  const char* argv[] = {compiler, kTacExecCStandard, source_path, "-o", out_path, NULL};
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

// Purpose: Execute a compiled test binary and collect its exit code.
// Inputs: path is the binary path to execute.
// Outputs: Returns true on success and fills exit_code.
// Invariants/Assumptions: Exit status matches main() return value.
static bool tac_exec_run_binary(const char* path, int* exit_code) {
  const char* argv[] = {path, NULL};
  return tac_exec_run_process(argv, false, exit_code);
}

// Purpose: Append static variable TAC entries to a program.
// Inputs: prog is the TAC program built from the AST.
// Outputs: Extends the TAC program with static variables from the symbol table.
// Invariants/Assumptions: global_symbol_table is valid until arena_destroy().
static void tac_exec_append_statics(struct TACProg* prog) {
  if (prog == NULL || global_symbol_table == NULL) {
    return;
  }
  for (size_t i = 0; i < global_symbol_table->size; i++) {
    for (struct SymbolEntry* entry = global_symbol_table->arr[i];
         entry != NULL;
         entry = entry->next) {
      struct TopLevel* top_level = symbol_to_TAC(entry);
      if (top_level != NULL) {
        if (prog->head == NULL) {
          prog->head = top_level;
          prog->tail = top_level;
        } else {
          prog->tail->next = top_level;
          prog->tail = top_level;
        }
      }
    }
  }
}

// Purpose: Build TAC from a source file and interpret it.
// Inputs: test is the test descriptor to run.
// Outputs: Returns true on success and fills out_result.
// Invariants/Assumptions: Uses the same pipeline as the compiler frontend.
static bool tac_exec_run_tac(const struct TacExecTest* test, int* out_result) {
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

  struct TACProg* tac_prog = prog_to_TAC(prog);
  if (tac_prog == NULL) {
    tac_exec_error(test->name, "tac", "TAC lowering failed");
    goto cleanup;
  }
  tac_exec_append_statics(tac_prog);

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

// Purpose: Run one TAC execution test and compare against the host compiler.
// Inputs: test is the test descriptor to run.
// Outputs: Returns true if TAC and host results match.
// Invariants/Assumptions: The host compiler result matches main()'s return value.
static bool tac_exec_run_test(const struct TacExecTest* test) {
  int tac_result = 0;
  if (!tac_exec_run_tac(test, &tac_result)) {
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

int main(void) {
  if (!tac_exec_ensure_dir(kTacExecBuildDir) || !tac_exec_ensure_dir(kTacExecOutDir)) {
    fprintf(stderr, "TAC exec tests: failed to create %s\n", kTacExecOutDir);
    return 1;
  }

  size_t total = sizeof(kTacExecTests) / sizeof(kTacExecTests[0]);
  size_t passed = 0;

  for (size_t i = 0; i < total; i++) {
    printf("- tac_exec_%s\n", kTacExecTests[i].name);
    if (tac_exec_run_test(&kTacExecTests[i])) {
      passed++;
    }
  }

  if (passed == total) {
    printf("TAC execution tests passed.\n");
    return 0;
  }

  printf("TAC execution tests failed: %zu / %zu passed.\n", passed, total);
  return 1;
}
