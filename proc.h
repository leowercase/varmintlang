#ifndef LANG_PROC_H
#define LANG_PROC_H

#include "val.h"
#include "pcode.h"

typedef struct Proc {
  GCData gc_data;
  Str name;
  PCode code;
} Proc;

#endif
