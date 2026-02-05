#ifndef LANG_HEART_H
#define LANG_HEART_H

#include "val.h"
#include "state.h"

/*
 * Heart of a Varmint, offspring of the C cockroach.
 * Contains all common state.
 */
typedef struct {
  CallStack call_stack;
  OpStack op_stack;
} Varmint;

Varmint varmint_start();
Value varmint_run(Varmint *vm, char *source);
void varmint_free(Varmint *vm);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
