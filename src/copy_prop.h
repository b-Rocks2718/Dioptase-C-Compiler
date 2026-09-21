#ifndef COPY_PROP_H
#define COPY_PROP_H

#include "cfg.h"
#include "slice.h"

struct ReachingCopyList {
  struct Slice* dst;
  struct Slice* src;
  struct ReachingCopyList* next;
};

// Performs copy propagation on the given control flow graph (CFG).
struct CFG* copy_prop(struct CFG* cfg);

#endif // COPY_PROP_H