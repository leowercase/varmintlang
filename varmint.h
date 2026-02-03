#ifndef LANG_HEART_H
#define LANG_HEART_H

#include "pcode.h"
#include "val.h"

/*
 * Heart of a Varmint, offspring of the C cockroach
 */
typedef struct {
  FnScope current_scope;
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
