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

int main(int argc, const char *const *const argv) {

    int print_tokens = 0;
    int print_ast = 0;
    int print_preprocess = 0;
    int saw_output_flag = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-tokens") == 0) {
            print_tokens = 1;
            saw_output_flag = 1;
            continue;
        }
        if (strcmp(arg, "-preprocess") == 0) {
            print_preprocess = 1;
            saw_output_flag = 1;
            continue;
        }
        if (strcmp(arg, "-ast") == 0) {
            print_ast = 1;
            saw_output_flag = 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg);
            fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] <file name>\n", argv[0]);
            exit(1);
        }
        if (filename == NULL) {
            filename = arg;
            continue;
        }

        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] <file name>\n", argv[0]);
        exit(1);
    }

    if (filename == NULL) {
        fprintf(stderr, "usage: %s [-preprocess] [-tokens] [-ast] <file name>\n", argv[0]);
        exit(1);
    }

    if (!saw_output_flag) {
        print_tokens = 0;
        print_ast = 0;
    }

    // open the file
    int fd = open(filename,O_RDONLY);
    if (fd < 0) {
        perror("open");
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
    char const* prog = (char const *)mmap(
        0,
        file_stats.st_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0);
    if (prog == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    char* preprocessed = preprocess(prog);
    size_t preprocessed_len = strlen(preprocessed);

    if (print_preprocess) {
        fputs(preprocessed, stdout);
        if (!print_tokens && !print_ast) {
            free(preprocessed);
            return 0;
        }
        if (preprocessed_len > 0 && preprocessed[preprocessed_len - 1] != '\n') {
            fputc('\n', stdout);
        }
    }

    struct TokenArray* tokens = lex(preprocessed);
    if (tokens == NULL) {
       free(preprocessed);
       return 1;
    }

    if (print_tokens) {
        print_token_array(tokens);
    }

    if (print_ast) {
        struct Statement* stmt = parse_test(tokens);
        if (stmt == NULL) {
           free(preprocessed);
           destroy_token_array(tokens);
           return 2;
        };

        printf("AST:\n");
        print_stmt(stmt, 0);
        printf("\n");

        destroy_stmt(stmt);
    }
    destroy_token_array(tokens);
    free(preprocessed);
    
    return 0;
}
