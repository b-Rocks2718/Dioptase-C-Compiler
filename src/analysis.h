#ifndef ANALYSIS_H
#define ANALYSIS_H

/* Fatal diagnostic helpers always terminate. Clang's analyzer may otherwise
 * stop inlining them and explore impossible return paths. This annotation is
 * intentionally analyzer-only: the self-hosted compiler need not understand
 * Clang attributes, and normal host/guest builds retain their existing ABI. */
#ifdef __clang_analyzer__
#include <assert.h>
#define ANALYSIS_NORETURN __attribute__((noreturn))
#define ANALYSIS_ASSUME(condition) assert(condition)
#else
#define ANALYSIS_NORETURN
#define ANALYSIS_ASSUME(condition) ((void)0)
#endif

#endif
