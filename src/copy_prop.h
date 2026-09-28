#ifndef COPY_PROP_H
#define COPY_PROP_H

#include "slice.h"
#include <stdbool.h>

struct CFG;
struct CFGNode;
struct Val;

// Return true if dst = src is safe to record for copy propagation.
// Requires identical types, char/signed char, or a source constant 0.
bool copy_is_type_safe(const struct Val* src, const struct Val* dst);

// Perform copy propagation on cfg, rewriting its instructions in place.
// aliased_vars lists statics and address-taken locals, which calls and stores
// may modify. Analysis storage is pass-local and freed before returning; the
// CFG's node index fields are renumbered to match cfg->nodes.
struct CFG* copy_prop(struct CFG* cfg, struct SliceList aliased_vars);

#endif // COPY_PROP_H
