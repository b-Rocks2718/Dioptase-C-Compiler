#ifndef CONSTANT_FOLD_H
#define CONSTANT_FOLD_H

#include "TAC.h"

enum ConstantFoldAction {
  CONSTANT_FOLD_UNCHANGED,
  CONSTANT_FOLD_REPLACE,
  CONSTANT_FOLD_DELETE,
};

struct TACInstr* constant_fold(struct TACInstr* body);

#endif // CONSTANT_FOLD_H