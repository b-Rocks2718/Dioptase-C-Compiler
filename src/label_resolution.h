#ifndef LABEL_RESOLUTION_H
#define LABEL_RESOLUTION_H

#include "AST.h"
#include <stdbool.h>

enum LabelType {
  LOOP,
  SWITCH
};

bool label_loops(struct Program* prog);

bool label_stmt(struct Slice* func_name, struct Statement* stmt);

bool label_block(struct Slice* func_name, struct Block* block);

bool resolve_gotos(struct Block* block);

bool collect_cases(struct Block* block);

#endif // LABEL_RESOLUTION_H