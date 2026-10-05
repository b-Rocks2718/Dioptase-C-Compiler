#include "checked_alloc.h"
#include "exit_codes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void* checked_calloc(size_t count, size_t size, const char* component, const char* purpose) {
  if (count != 0 && size > SIZE_MAX / count) {
    fprintf(stderr, "%s error: allocation size overflow while %s\n", component, purpose);
    exit(BCC_EXIT_INTERNAL);
  }
  void* allocation = calloc(count, size);
  if (allocation == NULL && count != 0 && size != 0) {
    fprintf(stderr, "%s error: unable to allocate %zu bytes while %s\n",
            component, count * size, purpose);
    exit(BCC_EXIT_INTERNAL);
  }
  return allocation;
}
