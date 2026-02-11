#ifndef LANG_PROC_H
#define LANG_PROC_H

#include "val.h"
#include "code.h"

/*
 * Procedure - a tool for abstraction.
 * Can be a program, can be a function in said program.
 * https://en.wikipedia.org/wiki/Function_(computer_programming)
 */
typedef struct Proc {
  GCData gc_data;
  Str name;
  int arity;
  PCode code;
} Proc;

static inline
Proc *proc_new(int arity)
{
  Proc *procedure = malloc(sizeof(Proc));
  if (procedure == NULL) exit(EX_OSERR);

  procedure->name = NULL_STR;
  procedure->code = new_p_code();
  procedure->arity = arity;
  return procedure;
}

#endif
