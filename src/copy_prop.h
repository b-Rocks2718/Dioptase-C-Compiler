#ifndef COPY_PROP_H
#define COPY_PROP_H

#include "slice.h"
#include <stdbool.h>

struct CFG;
struct CFGNode;
struct Val;

// One reaching copy: dst currently holds the value of src.
struct ReachingCopy {
  struct Val* dst;
  struct Val* src;
  struct ReachingCopy* next;
};

// Linked list of reaching copies with O(1) append via last.
struct ReachingCopyList {
  struct ReachingCopy* head;
  struct ReachingCopy* last;
};

// Return true if dst = src is safe to record for copy propagation.
// Requires identical types, char/signed char, or a source constant 0.
bool copy_is_type_safe(const struct Val* src, const struct Val* dst);

// Performs copy propagation on the given control flow graph (CFG).
struct CFG* copy_prop(struct CFG* cfg, struct SliceList aliased_vars);

#endif // COPY_PROP_H
