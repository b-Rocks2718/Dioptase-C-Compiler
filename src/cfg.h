#ifndef CFG_H
#define CFG_H

#include "TAC.h"

struct CFG {
  int dummy; // TODO
};

struct CFG* build_cfg(struct TACInstr* body);

struct TACInstr* rebuild_body(struct CFG* cfg);

#endif // CFG_H