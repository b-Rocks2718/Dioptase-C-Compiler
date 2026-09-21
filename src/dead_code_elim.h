#ifndef DEAD_CODE_ELIM_H
#define DEAD_CODE_ELIM_H

#include "TAC.h"
#include "cfg.h"

struct CFG* dead_code_elim(struct CFG* cfg);

#endif // DEAD_CODE_ELIM_H