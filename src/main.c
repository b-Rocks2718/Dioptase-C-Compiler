#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "preprocessor.h"
#include "token_array.h"
#include "lexer.h"
#include "parser.h"
#include "identifier_resolution.h"
#include "label_resolution.h"
#include "typechecking.h"
#include "TAC.h"
#include "arena.h"
#include "source_location.h"

// Purpose: Control where TAC interpreter result is emitted.
// Inputs/Outputs: When set, results are printed to stderr instead of stdout.
// Invariants/Assumptions: Only used for interpreter-only execution.
static const char* kTacInterpResultStderrEnv = "DIOPTASE_TACC_RESULT_STDERR";

int main(int argc, const char *const *const argv) {

    int print_tokens = 0;
    int print_ast = 0;
    int print_preprocess = 0;
    int print_idents = 0;
    int print_labels = 0;
    int print_types = 0;
    int print_tac = 0;
    int interpret_tac = 0;
    const char *filename = NULL;
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
        if (strcmp(arg, "-interp") == 0) {
            interpret_tac = 1;
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
            fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-interp] [-DNAME[=value]] <file name>\n", argv[0]);
            free(cli_defines);
            exit(1);
        }
        if (filename == NULL) {
            filename = arg;
            continue;
        }

        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-interp] [-DNAME[=value]] <file name>\n", argv[0]);
        free(cli_defines);
        exit(1);
    }

    if (filename == NULL) {
        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] [-idents] [-labels] [-types] [-tac] [-interp] [-DNAME[=value]] <file name>\n", argv[0]);
        free(cli_defines);
        exit(1);
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
        if (!print_tokens && !print_ast) {
            destroy_preprocess_result(&preprocessed);
            return 0;
        }
        if (preprocessed_len > 0 && preprocessed.text[preprocessed_len - 1] != '\n') {
            fputc('\n', stdout);
        }
    }

    struct TokenArray* tokens = lex(preprocessed.text);
    if (tokens == NULL) {
       destroy_preprocess_result(&preprocessed);
       return 1;
    }

    if (print_tokens) {
        print_token_array(tokens);
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

    if (!label_loops(prog)) {
        fprintf(stderr, "Loop labeling failed\n");
        destroy_preprocess_result(&preprocessed);
        destroy_token_array(tokens);
        arena_destroy();
        return 4;
    } else if (print_labels) {
        print_prog(prog);
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

    struct TACProg* tac_prog = NULL;
    if (print_tac || interpret_tac) {
        tac_prog = prog_to_TAC(prog);
        if (tac_prog != NULL && global_symbol_table != NULL) {
            // Collect static storage entries from the symbol table for visibility.
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
            tac_prog->statics = statics_head;
        }
    }

    if (print_tac) {
        print_tac_prog(tac_prog);
    }
    if (interpret_tac) {
        if (tac_prog == NULL) {
            fprintf(stderr, "TAC lowering failed\n");
            destroy_preprocess_result(&preprocessed);
            destroy_token_array(tokens);
            arena_destroy();
            return 6;
        }
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
