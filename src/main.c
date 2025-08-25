#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "preprocessor.h"
#include "token_array.h"
#include "lexer.h"
#include "parser.h"

int main(int argc, const char *const *const argv) {

    if (argc != 2) {
        fprintf(stderr,"usage: %s <file name>\n",argv[0]);
        exit(1);
    }

    // open the file
    int fd = open(argv[1],O_RDONLY);
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

    struct TokenArray* tokens = lex(preprocessed);
    if (tokens == NULL) {
       free(preprocessed);
       return 1;
    }

    printf("\nTokens:\n");
    print_token_array(tokens);

    printf("\n");

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
    destroy_token_array(tokens);
    free(preprocessed);
    
    return 0;
}