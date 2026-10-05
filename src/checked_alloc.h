#ifndef CHECKED_ALLOC_H
#define CHECKED_ALLOC_H

#include <stddef.h>

// calloc for compiler passes that cannot continue without the memory: a size
// overflow or allocation failure prints "<component> error: ... while
// <purpose>" and exits with BCC_EXIT_INTERNAL. A zero-sized request returns
// whatever calloc returns (possibly NULL) without error. Free with free().
void* checked_calloc(size_t count, size_t size, const char* component, const char* purpose);

#endif // CHECKED_ALLOC_H
