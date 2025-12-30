#ifndef LOOP_RESOLUTION_H
#define LOOP_RESOLUTION_H

#include "AST.h"
#include <stdbool.h>

enum LabelType {
  LOOP,
  SWITCH
};

bool label_loops(struct Program* prog);

bool label_stmt(struct Slice* func_name, struct Statement* stmt);

bool label_block(struct Slice* func_name, struct Block* block);

#endif // LOOP_RESOLUTION_H