#ifndef LANG_PROC_H
#define LANG_PROC_H

#include "val.h"
#include "pcode.h"

// Process.
typedef struct Proc {
  GCData gc_data;
  Str name;
  PCode code;
} Proc;

static inline
Proc *proc_new()
{
  Proc *proc = malloc(sizeof(Proc));
  if (proc == NULL) exit(EX_OSERR);

  proc->name = NULL_STR;
  proc->code = new_p_code();
  return proc;
}

#endif
