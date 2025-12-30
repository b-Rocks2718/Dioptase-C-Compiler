#ifndef UNIQUE_NAME_H
#define UNIQUE_NAME_H

#include "slice.h"

struct Slice* make_unique(struct Slice* original_name);

struct Slice* make_unique_label(struct Slice* func_name, const char* suffix);


#endif // UNIQUE_NAME_H