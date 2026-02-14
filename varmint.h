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
  CallFrame *frame;
  OpStack op_stack;
  NativesTable natives;
  Value result;
} Varmint;

#define add_native_fn(vm, name, fn, arity) \
  NativesTable_set(&(vm)->natives, str_from(name), native_fn(fn, arity))

Varmint varmint_start(void);
Value varmint_run(Varmint *vm, char *source);
void varmint_free(Varmint *vm);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
