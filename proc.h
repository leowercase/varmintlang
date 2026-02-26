#ifndef LANG_PROC_H
#define LANG_PROC_H

#include "str.h"
#include "val.h"
#include "code.h"

/*
 * Procedure - a tool for abstraction.
 * Can be a program, can be a function in said program.
 * https://en.wikipedia.org/wiki/Function_(computer_programming)
 */
typedef struct Procedure {
  GCData gc_data;
  Str name;
  size_t arity;
  PCode code;
} Procedure;

#endif
