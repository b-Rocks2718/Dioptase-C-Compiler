#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/wait.h>
#include <unistd.h>

#include "preprocessor.h"
#include "token_array.h"
#include "lexer.h"
#include "parser.h"
#include "identifier_resolution.h"
#include "label_resolution.h"
#include "typechecking.h"
#include "TAC.h"
#include "cfg.h"
#include "call_graph.h"
#include "optimization.h"
#include "asm_gen.h"
#include "codegen.h"
#include "regalloc.h"
#include "machine_print.h"
#include "arena.h"
#include "source_location.h"
#include "exit_codes.h"

// When set, results are printed to stderr instead of stdout.
// Only used for interpreter-only execution.
static const char* kTacInterpResultStderrEnv = "DIOPTASE_TACC_RESULT_STDERR";

static const char* kDefaultAsmOutputPath = "a.s";
static const char* kDefaultHexOutputPath = "a.hex";
static const char* kDefaultBinOutputPath = "a.bin";

// Environment variable that overrides the assembler path.
// Read via getenv when invoking the assembler.
static const char* kAssemblerEnvVar = "DIOPTASE_ASSEMBLER";

// Environment variable that points to the repo root for assembler lookup.
// Read via getenv when DIOPTASE_ASSEMBLER is unset.
static const char* kRepoRootEnvVar = "DIOPTASE_ROOT";

// Default assembler locations under the repo root.
// Joined with DIOPTASE_ROOT when DIOPTASE_ASSEMBLER is unset.
enum { kDefaultAssemblerRelPathCount = 2 };
static const char* const kDefaultAssemblerRelPaths[kDefaultAssemblerRelPathCount] = {
    "Dioptase-Assembler/build/debug/basm",
    "Dioptase-Assembler/build/release/basm",
};

// Default CRT directory under the repo root for user-mode compiler links.
// Joined with DIOPTASE_ROOT when no explicit CRT dir is requested.
// This compiler-local CRT may differ from the OS copy.
static const char* kDefaultCompilerCrtRelDir = "Dioptase-Languages/Dioptase-C-Compiler/crt";

// Suffix for temporary assembly files used during full compilation.
// Appended to the output path to form a temp asm name.
static const char* kAsmTempSuffix = ".s.tmp";

// Copy a string into heap storage.
// Caller must free the returned string.
static char* duplicate_string(const char* src) {
    if (src == NULL) return NULL;
    size_t len = strlen(src);
    char* copy = malloc(len + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

// Join two path components with a '/' separator when needed.
static char* join_paths(const char* left, const char* right) {
    if (left == NULL || right == NULL) return NULL;
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    int needs_sep = (left_len > 0 && left[left_len - 1] != '/');
    size_t total_len = left_len + (needs_sep ? 1 : 0) + right_len + 1;
    char* path = malloc(total_len);
    if (path == NULL) return NULL;
    if (needs_sep) {
        snprintf(path, total_len, "%s/%s", left, right);
    } else {
        snprintf(path, total_len, "%s%s", left, right);
    }
    return path;
}

// Check whether a path is a runnable file.
static bool is_executable_path(const char* path) {
    if (path == NULL || path[0] == '\0') return false;
    return access(path, X_OK) == 0;
}

// Choose the assembler binary path for full compilation.
// Returns a heap-allocated assembler path or NULL if none are available.
static char* select_assembler_path(void) {
    const char* env = getenv(kAssemblerEnvVar);
    if (env != NULL && env[0] != '\0') {
        return duplicate_string(env);
    }

    const char* repo_root = getenv(kRepoRootEnvVar);
    if (repo_root == NULL || repo_root[0] == '\0') {
        return NULL;
    }

    for (int i = 0; i < kDefaultAssemblerRelPathCount; ++i) {
        char* candidate = join_paths(repo_root, kDefaultAssemblerRelPaths[i]);
        if (candidate == NULL) {
            return NULL;
        }
        if (is_executable_path(candidate)) {
            return candidate;
        }
        free(candidate);
    }

    return NULL;
}

// Build a temporary assembly output path from the final output path.
// Returns a heap-allocated path string or NULL on allocation failure.
// Caller must free the returned string.
static char* make_temp_asm_path(const char* output_path) {
    size_t output_len = strlen(output_path);
    size_t suffix_len = strlen(kAsmTempSuffix);
    size_t total_len = output_len + suffix_len + 1;
    char* temp_path = malloc(total_len);
    if (temp_path == NULL) {
        fprintf(stderr, "Compiler Error: failed to allocate temp asm path\n");
        return NULL;
    }
    snprintf(temp_path, total_len, "%s%s", output_path, kAsmTempSuffix);
    return temp_path;
}

// Choose the default CRT directory for user-mode compiler links.
// Returns a heap-allocated CRT directory path or NULL if none is available.
static char* select_default_crt_dir(void) {
    const char* repo_root = getenv(kRepoRootEnvVar);
    if (repo_root == NULL || repo_root[0] == '\0') {
        return NULL;
    }

    return join_paths(repo_root, kDefaultCompilerCrtRelDir);
}

// Invoke the assembler to emit the final hex or binary file.
// emit_binary requests -bin output.
// Returns true on success and false on failure.
static bool run_assembler(const char* assembler_path,
                                   const char* asm_path,
                                   const char* output_path,
                                   bool kernel_mode,
                                   const char* crt_dir,
                                   bool emit_binary,
                                   int emit_debug_info) {
    if (assembler_path == NULL || asm_path == NULL || output_path == NULL) {
        fprintf(stderr, "Compiler Error: assembler invocation missing required paths\n");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Compiler Error: failed to launch assembler: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        const char* args[11];
        int arg_idx = 0;
        args[arg_idx++] = assembler_path;
        if (kernel_mode) {
            args[arg_idx++] = "-kernel";
        } else {
            if (crt_dir == NULL || crt_dir[0] == '\0') {
                fprintf(stderr,
                        "Compiler Error: user-mode links require a CRT directory. "
                        "Pass -crt <dir> or set %s.\n",
                        kRepoRootEnvVar);
                _exit(127);
            }
            args[arg_idx++] = "-crt";
            args[arg_idx++] = crt_dir;
        }
        if (emit_binary) {
            args[arg_idx++] = "-bin";
        }
        args[arg_idx++] = "-o";
        args[arg_idx++] = output_path;
        args[arg_idx++] = asm_path;
        if (emit_debug_info) {
            args[arg_idx++] = "-g";
        }
        args[arg_idx] = NULL;

        execvp(assembler_path, (char* const*)args);
        fprintf(stderr, "Compiler Error: exec failed for assembler %s: %s\n",
                assembler_path, strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "Compiler Error: failed to wait for assembler: %s\n", strerror(errno));
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Compiler Error: assembler failed with status %d\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }

    return true;
}

// Remove a temporary assembly file once assembling completes.
// Returns true if the file was removed or did not exist.
static bool remove_temp_asm(const char* path) {
    if (path == NULL || path[0] == '\0') return true;
    if (unlink(path) == 0) return true;
    if (errno == ENOENT) return true;
    fprintf(stderr, "Compiler Error: failed to remove temp asm %s: %s\n",
            path, strerror(errno));
    return false;
}

// Pipeline stages in execution order. A diagnostic flag prints one stage's
// result; the compiler stops after the latest stage any flag asks for, unless
// a full compile (STAGE_OUTPUT) is requested.
enum Stage {
  STAGE_PREPROCESS,
  STAGE_LEX,
  STAGE_PARSE,
  STAGE_RESOLVE,
  STAGE_LABELS,
  STAGE_TYPES,
  STAGE_TAC,
  STAGE_ASM,
  STAGE_INTERP,
  STAGE_OUTPUT, // machine code, then the assembler/linker unless -s
};

// Diagnostics selectable on the command line.
enum Diagnostic {
  DIAG_PREPROCESS,
  DIAG_TOKENS,
  DIAG_AST,
  DIAG_IDENTS,
  DIAG_LABELS,
  DIAG_TYPES,
  DIAG_TAC,
  DIAG_CFG,
  DIAG_CALL_GRAPH,
  DIAG_ASM,
  DIAG_INTERP,
  DIAG_COUNT,
};

// Command-line spelling of each diagnostic and the stage whose result it shows.
static const struct {
  const char* name;
  enum Diagnostic diagnostic;
  enum Stage stage;
} kDiagnosticFlags[] = {
  {"-preprocess", DIAG_PREPROCESS, STAGE_PREPROCESS},
  {"-tokens", DIAG_TOKENS, STAGE_LEX},
  {"-ast", DIAG_AST, STAGE_PARSE},
  {"-idents", DIAG_IDENTS, STAGE_RESOLVE},
  {"-labels", DIAG_LABELS, STAGE_LABELS},
  {"-types", DIAG_TYPES, STAGE_TYPES},
  {"-tac", DIAG_TAC, STAGE_TAC},
  {"-cfg", DIAG_CFG, STAGE_TAC},
  {"-cg", DIAG_CALL_GRAPH, STAGE_TAC},
  {"-asm", DIAG_ASM, STAGE_ASM},
  {"-interp", DIAG_INTERP, STAGE_INTERP},
};

// Individual optimization switches; -opt turns on every one of them.
static const struct {
  const char* name;
  size_t offset; // of the bool in struct OptimizationOptions
} kOptimizationFlags[] = {
  {"-constant-fold", offsetof(struct OptimizationOptions, constant_fold)},
  {"-dead-code", offsetof(struct OptimizationOptions, dead_code_elim)},
  {"-copy-prop", offsetof(struct OptimizationOptions, copy_prop)},
  {"-dead-store", offsetof(struct OptimizationOptions, dead_store_elim)},
  {"-tail-call", offsetof(struct OptimizationOptions, tail_call_opt)},
  {"-inline", offsetof(struct OptimizationOptions, inline_opt)},
  {"-peephole", offsetof(struct OptimizationOptions, peephole_opt)},
  {"-reg-alloc", offsetof(struct OptimizationOptions, reg_alloc)},
};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

// Everything the command line selects.
struct CommandLine {
  bool diagnostics[DIAG_COUNT];
  enum Stage last_stage; // stop after this stage
  bool emit_asm_file;    // -s: write assembly instead of assembling
  bool emit_binary;      // -bin: binary instead of hex output
  bool emit_debug_info;  // -g
  bool kernel_mode;      // -kernel
  const char* filename;
  const char* output_path;
  const char* crt_dir;   // -crt
  const char** defines;  // -D values, heap array owned by the caller
  int num_defines;
  struct OptimizationOptions optimization;
};

// Print the usage line to stderr.
static void print_usage(const char* argv0) {
  fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-cfg] [-cg] [-asm] [-interp] [-s] [-bin] [-g] [-kernel] [-crt <dir>] [-o <file>] [-DNAME[=value]] <file name>\n", argv0);
}

// Report a command-line error and exit with BCC_EXIT_INPUT.
static void command_line_error(struct CommandLine* cl, const char* argv0, const char* message,
                               const char* detail, bool usage) {
  if (message != NULL) {
    fprintf(stderr, message, detail);
  }
  if (usage) {
    print_usage(argv0);
  }
  free(cl->defines);
  exit(BCC_EXIT_INPUT);
}

// Turn on the switch for a named optimization; false if name is not one.
static bool set_optimization_flag(struct OptimizationOptions* options, const char* name) {
  for (size_t i = 0; i < ARRAY_LEN(kOptimizationFlags); i++) {
    if (strcmp(name, kOptimizationFlags[i].name) == 0) {
      *(bool*)((char*)options + kOptimizationFlags[i].offset) = true;
      return true;
    }
  }
  if (strcmp(name, "-opt") == 0) {
    for (size_t i = 0; i < ARRAY_LEN(kOptimizationFlags); i++) {
      *(bool*)((char*)options + kOptimizationFlags[i].offset) = true;
    }
    return true;
  }
  return false;
}

// Parse argv into cl, exiting with BCC_EXIT_INPUT on errors.
static void parse_command_line(int argc, const char* const* argv, struct CommandLine* cl) {
  memset(cl, 0, sizeof(*cl));
  cl->defines = malloc(argc * sizeof(char*));
  bool output_path_set = false;
  bool any_diagnostic = false;
  enum Stage last_diagnostic_stage = STAGE_PREPROCESS;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    bool matched = false;
    for (size_t d = 0; d < ARRAY_LEN(kDiagnosticFlags); d++) {
      if (strcmp(arg, kDiagnosticFlags[d].name) == 0) {
        cl->diagnostics[kDiagnosticFlags[d].diagnostic] = true;
        any_diagnostic = true;
        if (kDiagnosticFlags[d].stage > last_diagnostic_stage) {
          last_diagnostic_stage = kDiagnosticFlags[d].stage;
        }
        matched = true;
        break;
      }
    }
    if (matched || set_optimization_flag(&cl->optimization, arg)) {
      continue;
    }
    if (strcmp(arg, "-s") == 0) {
      cl->emit_asm_file = true;
    } else if (strcmp(arg, "-bin") == 0) {
      cl->emit_binary = true;
    } else if (strcmp(arg, "-g") == 0) {
      cl->emit_debug_info = true;
    } else if (strcmp(arg, "-kernel") == 0) {
      cl->kernel_mode = true;
    } else if (strcmp(arg, "-crt") == 0) {
      if (i + 1 >= argc) {
        command_line_error(cl, argv[0], "option -crt requires a CRT directory path\n", NULL, false);
      }
      cl->crt_dir = argv[++i];
    } else if (strcmp(arg, "-o") == 0) {
      if (i + 1 >= argc) {
        command_line_error(cl, argv[0], "option -o requires an output file path\n", NULL, false);
      }
      cl->output_path = argv[++i];
      output_path_set = true;
    } else if (strncmp(arg, "-D", 2) == 0) {
      if (arg[2] == '\0') {
        command_line_error(cl, argv[0],
                           "Invalid -D definition (expected -DNAME or -DNAME=value)\n", NULL, false);
      }
      cl->defines[cl->num_defines++] = arg + 2;
    } else if (arg[0] == '-') {
      command_line_error(cl, argv[0], "unknown option: %s\n", arg, true);
    } else if (cl->filename == NULL) {
      cl->filename = arg;
    } else {
      command_line_error(cl, argv[0], NULL, NULL, true);
    }
  }

  if (cl->filename == NULL) {
    command_line_error(cl, argv[0], NULL, NULL, true);
  }
  if (cl->kernel_mode && cl->crt_dir != NULL) {
    command_line_error(cl, argv[0], "option -crt is only valid for user-mode links\n", NULL, false);
  }
  if (!output_path_set) {
    cl->output_path = cl->emit_asm_file ? kDefaultAsmOutputPath
                      : cl->emit_binary ? kDefaultBinOutputPath
                                        : kDefaultHexOutputPath;
  }
  // Diagnostics normally stop after the latest stage they show. An explicit -s
  // still requests a file, so it carries the compile through machine assembly.
  cl->last_stage = (!any_diagnostic || cl->emit_asm_file) ? STAGE_OUTPUT : last_diagnostic_stage;
}

// Resources owned by one compilation after preprocessing succeeds.
struct Compilation {
  struct PreprocessResult preprocessed;
  struct TokenArray* tokens;
  bool arena_live;
};

// Release a compilation's resources and return status as the exit code.
static int finish(struct Compilation* c, int status) {
  if (c->arena_live) {
    arena_destroy();
  }
  if (c->tokens != NULL) {
    destroy_token_array(c->tokens);
  }
  destroy_preprocess_result(&c->preprocessed);
  return status;
}

// Print each function's control-flow graph (-cfg).
static void print_cfg_graphs(const struct TACProg* tac_prog) {
  bool printed_function = false;
  for (const struct TopLevel* top = tac_prog->head; top != NULL; top = top->next) {
    if (top->type != FUNC) {
      continue;
    }
    if (printed_function) {
      printf("\n");
    }
    printf("Function ");
    print_slice(top->top.tac_func.name);
    printf("\n");
    print_cfg(build_cfg(top->top.tac_func.body.head));
    printed_function = true;
  }
  if (!printed_function) {
    printf("CFG: no function definitions\n");
  }
}

// Write machine code and, unless -s was given, assemble and link it into the
// requested output. Returns BCC_EXIT_OK or BCC_EXIT_OUTPUT.
static int emit_output(const struct CommandLine* cl, struct MachineProg* machine_prog) {
  // Read once: every cleanup below depends on which branch allocated the path.
  bool assemble = !cl->emit_asm_file;
  const char* asm_output_path = cl->output_path;
  char* asm_output_path_alloc = NULL;
  if (assemble) {
    asm_output_path_alloc = make_temp_asm_path(cl->output_path);
    if (asm_output_path_alloc == NULL) {
      return BCC_EXIT_OUTPUT;
    }
    asm_output_path = asm_output_path_alloc;
  }

  if (!write_machine_prog_to_file(machine_prog, asm_output_path)) {
    fprintf(stderr, "ASM generation failed: unable to write %s\n", asm_output_path);
    free(asm_output_path_alloc);
    return BCC_EXIT_OUTPUT;
  }
  if (!assemble) {
    return BCC_EXIT_OK;
  }

  int status = BCC_EXIT_OK;
  char* assembler_path = select_assembler_path();
  char* crt_dir = NULL;
  if (assembler_path == NULL) {
    fprintf(stderr,
            "Compiler Error: unable to find assembler. Set %s or %s so basm can be located.\n",
            kAssemblerEnvVar, kRepoRootEnvVar);
    status = BCC_EXIT_OUTPUT;
  } else if (!cl->kernel_mode &&
             (crt_dir = cl->crt_dir != NULL ? duplicate_string(cl->crt_dir)
                                            : select_default_crt_dir()) == NULL) {
    fprintf(stderr,
            "Compiler Error: unable to resolve a user CRT directory. "
            "Pass -crt <dir> or set %s.\n",
            kRepoRootEnvVar);
    status = BCC_EXIT_OUTPUT;
  } else if (!run_assembler(assembler_path, asm_output_path, cl->output_path, cl->kernel_mode,
                            crt_dir, cl->emit_binary, cl->emit_debug_info)) {
    status = BCC_EXIT_OUTPUT;
  }
  free(assembler_path);
  free(crt_dir);
  remove_temp_asm(asm_output_path);
  free(asm_output_path_alloc);
  return status;
}

// Run the requested compiler stages and emit the selected diagnostics or output.
int main(int argc, const char *const *const argv) {
  struct CommandLine cl;
  parse_command_line(argc, argv, &cl);
  const bool* show = cl.diagnostics;

  // Map the source file into memory.
  int fd = open(cl.filename, O_RDONLY);
  if (fd < 0) {
    perror("open");
    free(cl.defines);
    exit(BCC_EXIT_INPUT);
  }
  struct stat file_stats;
  if (fstat(fd, &file_stats) != 0) {
    perror("fstat");
    exit(BCC_EXIT_INPUT);
  }
  char const* text = (char const*)mmap(0, file_stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (text == MAP_FAILED) {
    perror("mmap");
    exit(BCC_EXIT_INPUT);
  }

  struct Compilation c;
  memset(&c, 0, sizeof(c));
  if (!preprocess(text, cl.filename, cl.num_defines, cl.defines, &c.preprocessed)) {
    free(cl.defines);
    return BCC_EXIT_INPUT;
  }
  free(cl.defines);
  set_source_context_with_map(cl.filename, c.preprocessed.text, &c.preprocessed.map);
  if (show[DIAG_PREPROCESS]) {
    size_t len = strlen(c.preprocessed.text);
    fputs(c.preprocessed.text, stdout);
    if (len > 0 && c.preprocessed.text[len - 1] != '\n') {
      fputc('\n', stdout);
    }
  }
  if (cl.last_stage == STAGE_PREPROCESS) {
    return finish(&c, BCC_EXIT_OK);
  }

  c.tokens = lex(c.preprocessed.text);
  if (c.tokens == NULL) {
    return finish(&c, BCC_EXIT_INPUT);
  }
  if (show[DIAG_TOKENS]) {
    print_token_array(c.tokens);
  }
  if (cl.last_stage == STAGE_LEX) {
    return finish(&c, BCC_EXIT_OK);
  }

  arena_init(16384);
  c.arena_live = true;
  struct Program* prog = parse_prog(c.tokens);
  if (prog == NULL) {
    return finish(&c, BCC_EXIT_PARSE);
  }
  // The AST keeps only payload slices and source pointers, not tokens, so
  // the token entries can go now; their slices live until the final cleanup.
  token_array_release_tokens(c.tokens);
  if (show[DIAG_AST]) {
    print_prog(prog);
  }
  if (cl.last_stage == STAGE_PARSE) {
    return finish(&c, BCC_EXIT_OK);
  }

  if (!resolve_prog(prog)) {
    fprintf(stderr, "Identifier resolution failed\n");
    return finish(&c, BCC_EXIT_RESOLVE);
  }
  if (show[DIAG_IDENTS]) {
    print_prog(prog);
  }
  if (cl.last_stage == STAGE_RESOLVE) {
    return finish(&c, BCC_EXIT_OK);
  }

  if (!label_loops(prog)) {
    fprintf(stderr, "Loop labeling failed\n");
    return finish(&c, BCC_EXIT_LABELS);
  }
  if (show[DIAG_LABELS]) {
    print_prog(prog);
  }
  if (cl.last_stage == STAGE_LABELS) {
    return finish(&c, BCC_EXIT_OK);
  }

  if (!typecheck_program(prog)) {
    fprintf(stderr, "Typechecking failed\n");
    return finish(&c, BCC_EXIT_TYPES);
  }
  if (show[DIAG_TYPES]) {
    print_symbol_table(global_symbol_table);
    print_prog(prog);
  }
  if (cl.last_stage == STAGE_TYPES) {
    return finish(&c, BCC_EXIT_OK);
  }

  struct TACProg* tac_prog = prog_to_TAC(prog, cl.emit_debug_info, cl.optimization.tail_call_opt);
  if (tac_prog == NULL) {
    fprintf(stderr, "TAC lowering failed\n");
    return finish(&c, BCC_EXIT_INTERNAL);
  }
  optimize(tac_prog, cl.optimization);
  if (show[DIAG_TAC]) {
    print_symbol_table(global_symbol_table);
    print_tac_prog(tac_prog);
  }
  if (show[DIAG_CFG]) {
    print_cfg_graphs(tac_prog);
  }
  if (show[DIAG_CALL_GRAPH]) {
    struct CallGraph call_graph = build_call_graph(tac_prog);
    print_call_graph(&call_graph);
  }
  if (cl.last_stage == STAGE_TAC) {
    return finish(&c, BCC_EXIT_OK);
  }

  // Assembly is built for a full compile or for -asm; the interpreter alone
  // runs on TAC.
  if (cl.last_stage == STAGE_OUTPUT || show[DIAG_ASM]) {
    // Always emit section directives so kernel/user outputs share layout markers.
    struct AsmProg* asm_prog = prog_to_asm(tac_prog, true);
    if (asm_prog == NULL) {
      fprintf(stderr, "ASM generation failed: asm_gen returned NULL\n");
      return finish(&c, BCC_EXIT_INTERNAL);
    }
    // Instruction selection leaves pseudos; the allocator assigns some to
    // registers and assign_stack_slots places the rest in the frame.
    allocate_registers(asm_prog); // TODO: this should only happen with regalloc optimization enabled
    assign_stack_slots(asm_prog);
    if (show[DIAG_ASM]) {
      print_asm_symbol_table(asm_symbol_table);
      print_asm_prog(asm_prog);
    }
    if (cl.last_stage == STAGE_ASM) {
      return finish(&c, BCC_EXIT_OK);
    }

    if (cl.last_stage == STAGE_OUTPUT) {
      struct MachineProg* machine_prog = prog_to_machine(asm_prog);
      if (machine_prog == NULL) {
        fprintf(stderr, "ASM generation failed: codegen returned NULL\n");
        return finish(&c, BCC_EXIT_INTERNAL);
      }
      int status = emit_output(&cl, machine_prog);
      if (status != BCC_EXIT_OK) {
        return finish(&c, status);
      }
    }
  }

  if (show[DIAG_INTERP]) {
    int interp_result = tac_interpret_prog(tac_prog);
    const char* result_to_stderr = getenv(kTacInterpResultStderrEnv);
    if (result_to_stderr != NULL && result_to_stderr[0] != '\0') {
      fprintf(stderr, "%d\n", interp_result);
    } else {
      printf("%d\n", interp_result);
    }
  }
  return finish(&c, BCC_EXIT_OK);
}
