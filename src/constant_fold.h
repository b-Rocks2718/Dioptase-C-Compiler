#ifndef CONSTANT_FOLD_H
#define CONSTANT_FOLD_H

#include "TAC.h"

enum ConstantFoldAction {
  CONSTANT_FOLD_UNCHANGED,
  CONSTANT_FOLD_REPLACE,
  CONSTANT_FOLD_DELETE,
};

// Performs constant folding on the given three-address code (TAC) instruction list.
// The list is edited in place; the returned list has head and last updated.
struct TACInstrList constant_fold(struct TACInstrList body);

#endif // CONSTANT_FOLD_H