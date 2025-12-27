#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

// Preprocess a source buffer: strip comments, apply simple directives, and expand
// object-like macros. Includes are resolved relative to filename. The defines
// list contains strings from -DNAME or -DNAME=value.
char* preprocess(char const* prog, const char* filename, int num_defines, const char* const* defines);

#endif
