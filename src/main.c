#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "asm_gen.h"
#include "codegen.h"
#include "machine_print.h"
#include "arena.h"
#include "source_location.h"

// Purpose: Control where TAC interpreter result is emitted.
// Inputs/Outputs: When set, results are printed to stderr instead of stdout.
// Invariants/Assumptions: Only used for interpreter-only execution.
static const char* kTacInterpResultStderrEnv = "DIOPTASE_TACC_RESULT_STDERR";

// Purpose: Default output path for compiler-only assembly emission.
// Inputs/Outputs: Used when -s is set and no -o is provided.
// Invariants/Assumptions: Relative to the current working directory.
static const char* kDefaultAsmOutputPath = "a.s";

// Purpose: Default output path for assembled hex emission.
// Inputs/Outputs: Used when -s is not set and no -o is provided.
// Invariants/Assumptions: Relative to the current working directory.
static const char* kDefaultHexOutputPath = "a.hex";

// Purpose: Environment variable that overrides the assembler path.
// Inputs/Outputs: Read via getenv when invoking the assembler.
// Invariants/Assumptions: If set, must point to an executable binary.
static const char* kAssemblerEnvVar = "DIOPTASE_ASSEMBLER";

// Purpose: Known default assembler locations within the repo.
// Inputs/Outputs: Probed in order when DIOPTASE_ASSEMBLER is unset.
// Invariants/Assumptions: Relative to the current working directory.
enum { kDefaultAssemblerPathCount = 2 };
static const char* const kDefaultAssemblerPaths[kDefaultAssemblerPathCount] = {
    "/home/brooks/Dioptase/Dioptase-Assembler/build/debug/basm",
    "/home/brooks/Dioptase/Dioptase-Assembler/build/release/basm",
};

// Purpose: Suffix for temporary assembly files used during full compilation.
// Inputs/Outputs: Appended to the output path to form a temp asm name.
// Invariants/Assumptions: Resulting temp path should not collide with user files.
static const char* kAsmTempSuffix = ".s.tmp";

// Purpose: Check whether a path is a runnable file.
// Inputs: path is the filesystem path to probe.
// Outputs: Returns true if path is executable, false otherwise.
// Invariants/Assumptions: Uses access(2) and requires a POSIX-like host.
static bool is_executable_path(const char* path) {
    if (path == NULL || path[0] == '\0') return false;
    return access(path, X_OK) == 0;
}

// Purpose: Choose the assembler binary path for full compilation.
// Inputs: None.
// Outputs: Returns a usable assembler path or NULL if none are available.
// Invariants/Assumptions: Honors DIOPTASE_ASSEMBLER when provided.
static const char* select_assembler_path(void) {
    const char* env = getenv(kAssemblerEnvVar);
    if (env != NULL && env[0] != '\0') {
        return env;
    }

    for (int i = 0; i < kDefaultAssemblerPathCount; ++i) {
        if (is_executable_path(kDefaultAssemblerPaths[i])) {
            return kDefaultAssemblerPaths[i];
        }
    }

    return NULL;
}

// Purpose: Build a temporary assembly output path from the final output path.
// Inputs: output_path is the final assembler output path (e.g., a.hex).
// Outputs: Returns a heap-allocated path string or NULL on allocation failure.
// Invariants/Assumptions: Caller must free the returned string.
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

// Purpose: Invoke the assembler with -crt to emit the final hex file.
// Inputs: assembler_path is the executable path, asm_path is the input assembly,
//         output_path is the desired output file, kernel_mode forwards -kernel.
// Outputs: Returns true on success and false on failure.
// Invariants/Assumptions: Uses fork/exec to avoid shell interpretation.
static bool run_assembler(const char* assembler_path,
                                   const char* asm_path,
                                   const char* output_path,
                                   bool kernel_mode) {
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
        const char* args[6];
        int arg_idx = 0;
        args[arg_idx++] = assembler_path;
        if (kernel_mode) {
            args[arg_idx++] = "-kernel";
        } else {
            args[arg_idx++] = "-crt";
        }
        args[arg_idx++] = "-o";
        args[arg_idx++] = output_path;
        args[arg_idx++] = asm_path;
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

// Purpose: Remove a temporary assembly file once assembling completes.
// Inputs: path is the temporary assembly file path.
// Outputs: Returns true if the file was removed or did not exist.
// Invariants/Assumptions: Uses unlink(2) and requires a POSIX-like host.
static bool remove_temp_asm(const char* path) {
    if (path == NULL || path[0] == '\0') return true;
    if (unlink(path) == 0) return true;
    if (errno == ENOENT) return true;
    fprintf(stderr, "Compiler Error: failed to remove temp asm %s: %s\n",
            path, strerror(errno));
    return false;
}

// Purpose: Entry point for the C compiler frontend and debug pipelines.
// Inputs: argv contains command-line flags and the input file path.
// Outputs: Returns a non-zero status code on compilation or pipeline errors.
// Invariants/Assumptions: The input file is a regular file readable via mmap.
int main(int argc, const char *const *const argv) {

    int print_tokens = 0;
    int print_ast = 0;
    int print_preprocess = 0;
    int print_idents = 0;
    int print_labels = 0;
    int print_types = 0;
    int print_tac = 0;
    int print_asm = 0;
    int interpret_tac = 0;
    int kernel_mode = 0;
    const char *filename = NULL;
    const char *output_path = NULL;
    int output_path_set = 0;
    int emit_asm_file = 0;
    const char **cli_defines = malloc(argc * sizeof(char*));
    int num_defines = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-tokens") == 0) {
            print_tokens = 1;
            continue;
        }
        if (strcmp(arg, "-preprocess") == 0) {
            print_preprocess = 1;
            continue;
        }
        if (strcmp(arg, "-ast") == 0) {
            print_ast = 1;
            continue;
        }
        if (strcmp(arg, "-idents") == 0) {
            print_idents = 1;
            continue;
        }
        if (strcmp(arg, "-labels") == 0) {
            print_labels = 1;
            continue;
        }
        if (strcmp(arg, "-types") == 0) {
            print_types = 1;
            continue;
        }
        if (strcmp(arg, "-tac") == 0) {
            print_tac = 1;
            continue;
        }
        if (strcmp(arg, "-asm") == 0) {
            print_asm = 1;
            continue;
        }
        if (strcmp(arg, "-interp") == 0) {
            interpret_tac = 1;
            continue;
        }
        if (strcmp(arg, "-s") == 0) {
            emit_asm_file = 1;
            continue;
        }
        if (strcmp(arg, "-kernel") == 0) {
            kernel_mode = 1;
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "option -o requires an output file path\n");
                free(cli_defines);
                exit(1);
            }
            output_path = argv[++i];
            output_path_set = 1;
            continue;
        }
        if (strncmp(arg, "-D", 2) == 0) {
            const char *def = arg + 2;
            if (def[0] == '\0') {
                fprintf(stderr, "Invalid -D definition (expected -DNAME or -DNAME=value)\n");
                free(cli_defines);
                exit(1);
            }
            cli_defines[num_defines++] = def;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg);
            fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-asm] [-interp] [-s] [-kernel] [-o <file>] [-DNAME[=value]] <file name>\n", argv[0]);
            free(cli_defines);
            exit(1);
        }
        if (filename == NULL) {
            filename = arg;
            continue;
        }

        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-asm] [-interp] [-s] [-kernel] [-o <file>] [-DNAME[=value]] <file name>\n", argv[0]);
        free(cli_defines);
        exit(1);
    }

    if (filename == NULL) {
        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-asm] [-interp] [-s] [-kernel] [-o <file>] [-DNAME[=value]] <file name>\n", argv[0]);
        free(cli_defines);
        exit(1);
    }

    if (!output_path_set) {
        output_path = emit_asm_file ? kDefaultAsmOutputPath : kDefaultHexOutputPath;
    }

    // open the file
    int fd = open(filename,O_RDONLY);
    if (fd < 0) {
        perror("open");
        free(cli_defines);
        exit(1);
    }

    // determine its size (std::filesystem::get_size?)
    struct stat file_stats;
    int rc = fstat(fd,&file_stats);
    if (rc != 0) {
        perror("fstat");
        exit(1);
    }

    // map the file in my address space
    char const* text = (char const *)mmap(
        0,
        file_stats.st_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0);
    if (text == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct PreprocessResult preprocessed = {0};
    if (!preprocess(text, filename, num_defines, cli_defines, &preprocessed)) {
        free(cli_defines);
        return 1;
    }
    free(cli_defines);
    size_t preprocessed_len = strlen(preprocessed.text);
    set_source_context_with_map(filename, preprocessed.text, &preprocessed.map);

    if (print_preprocess) {
        fputs(preprocessed.text, stdout);
        if (preprocessed_len > 0 && preprocessed.text[preprocessed_len - 1] != '\n') {
            fputc('\n', stdout);
        }
    }
    const bool any_stage_flag =
        print_preprocess || print_tokens || print_ast || print_idents ||
        print_labels || print_types || print_tac || print_asm || interpret_tac;
    const bool run_full = !any_stage_flag;
    const bool stop_after_preprocess =
        any_stage_flag && print_preprocess &&
        !(print_tokens || print_ast || print_idents || print_labels ||
          print_types || print_tac || print_asm || interpret_tac);

    if (stop_after_preprocess) {
        destroy_preprocess_result(&preprocessed);
        return 0;
    }

    struct TokenArray* tokens = lex(preprocessed.text);
    if (tokens == NULL) {
       destroy_preprocess_result(&preprocessed);
       return 1;
    }

    if (print_tokens) {
        print_token_array(tokens);
    }
    const bool stop_after_tokens =
        any_stage_flag && print_tokens &&
        !(print_ast || print_idents || print_labels ||
          print_types || print_tac || print_asm || interpret_tac);
    if (stop_after_tokens) {
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        return 0;
    }

    arena_init(16384);
    struct Program* prog = parse_prog(tokens);
    if (prog == NULL) {
       destroy_preprocess_result(&preprocessed);
       destroy_token_array(tokens);
       arena_destroy();
       return 2;
    };

    if (print_ast)  {
        print_prog(prog);
    }
    const bool stop_after_ast =
        any_stage_flag && print_ast &&
        !(print_idents || print_labels ||
          print_types || print_tac || print_asm || interpret_tac);
    if (stop_after_ast) {
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 0;
    }

    // perform identifier resolution
    if (!resolve_prog(prog)) {
        fprintf(stderr, "Identifier resolution failed\n");
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 3;
    } else  if (print_idents) {
        print_prog(prog);
    }
    const bool stop_after_idents =
        any_stage_flag && print_idents &&
        !(print_labels || print_types || print_tac || print_asm || interpret_tac);
    if (stop_after_idents) {
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 0;
    }

    if (!label_loops(prog)) {
        fprintf(stderr, "Loop labeling failed\n");
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 4;
    } else if (print_labels) {
        print_prog(prog);
    }
    const bool stop_after_labels =
        any_stage_flag && print_labels &&
        !(print_types || print_tac || print_asm || interpret_tac);
    if (stop_after_labels) {
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 0;
    }

    if (!typecheck_program(prog)) {
        fprintf(stderr, "Typechecking failed\n");
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 5;
    } else if (print_types) {
        print_symbol_table(global_symbol_table);
        print_prog(prog);
    }
    const bool stop_after_types =
        any_stage_flag && print_types &&
        !(print_tac || print_asm || interpret_tac);
    if (stop_after_types) {
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 0;
    }

    struct TACProg* tac_prog = NULL;
    struct AsmProg* asm_prog = NULL;

    if (run_full || print_tac || print_asm || interpret_tac) {
        tac_prog = prog_to_TAC(prog);
        if (tac_prog == NULL) {
            fprintf(stderr, "TAC lowering failed\n");
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 6;
        }

        if (print_tac) {
            print_tac_prog(tac_prog);
        }
        const bool stop_after_tac =
            any_stage_flag && print_tac &&
            !(print_asm || interpret_tac);
        if (stop_after_tac) {
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 0;
        }
    }

    if (run_full || print_asm) {
        // Kernel-mode assembly is linear; omit user-mode section directives.
        const bool emit_sections = !kernel_mode;
        asm_prog = prog_to_asm(tac_prog, emit_sections);
        if (asm_prog == NULL) {
            fprintf(stderr, "ASM generation failed: asm_gen returned NULL\n");
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 6;
        }

        if (print_asm) {
            print_asm_prog(asm_prog);
        }
        const bool stop_after_asm =
            any_stage_flag && print_asm && !interpret_tac;
        if (stop_after_asm) {
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 0;
        }
    }

    if (run_full) {
        struct MachineProg* machine_prog = prog_to_machine(asm_prog);
        if (machine_prog == NULL) {
            fprintf(stderr, "ASM generation failed: codegen returned NULL\n");
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 6;
        }

        const char* asm_output_path = output_path;
        char* asm_output_path_alloc = NULL;
        if (!emit_asm_file) {
            asm_output_path_alloc = make_temp_asm_path(output_path);
            if (asm_output_path_alloc == NULL) {
                destroy_preprocess_result(&preprocessed);
                destroy_token_array(tokens);
                arena_destroy();
                return 6;
            }
            asm_output_path = asm_output_path_alloc;
        }

        if (!write_machine_prog_to_file(machine_prog, asm_output_path)) {
            fprintf(stderr, "ASM generation failed: unable to write %s\n", asm_output_path);
            free(asm_output_path_alloc);
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 6;
        }

        if (!emit_asm_file) {
            const char* assembler_path = select_assembler_path();
            if (assembler_path == NULL) {
                fprintf(stderr, "Compiler Error: unable to find assembler. Set %s or build Dioptase-Assembler.\n",
                        kAssemblerEnvVar);
                remove_temp_asm(asm_output_path);
                free(asm_output_path_alloc);
                destroy_preprocess_result(&preprocessed);
                destroy_token_array(tokens);
                arena_destroy();
                return 6;
            }
            if (!run_assembler(assembler_path, asm_output_path, output_path, kernel_mode)) {
                remove_temp_asm(asm_output_path);
                free(asm_output_path_alloc);
                destroy_preprocess_result(&preprocessed);
                destroy_token_array(tokens);
                arena_destroy();
                return 6;
            }
            remove_temp_asm(asm_output_path);
        }
        free(asm_output_path_alloc);
    }
    if (interpret_tac) {
        int interp_result = tac_interpret_prog(tac_prog);
        const char* result_to_stderr = getenv(kTacInterpResultStderrEnv);
        if (result_to_stderr != NULL && result_to_stderr[0] != '\0') {
            fprintf(stderr, "%d\n", interp_result);
        } else {
            printf("%d\n", interp_result);
        }
    }
    
    arena_destroy();
    destroy_token_array(tokens);
    destroy_preprocess_result(&preprocessed);
    
    return 0;
}
