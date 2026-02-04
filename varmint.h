#ifndef LANG_HEART_H
#define LANG_HEART_H

#include "pcode.h"
#include "val.h"

// Stack used for operations.
typedef DYN_ARRAY_STRUCT(Value) Stack;
#define T Value
#define ARR Stack
#include "generic/dyn_array.inc"

/*
 * Heart of a Varmint, offspring of the C cockroach
 */
typedef struct {
  Stack stack;
  uint8_t *ip;
  Value result;
} Varmint;

Varmint varmint_start();
void varmint_free(Varmint *vm);

Value varmint_run(Varmint *vm, char *source);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
