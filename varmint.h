#ifndef LANG_HEART_H
#define LANG_HEART_H

#include "ir.h"
#include "str.h"
#include "util.h"
#include "val.h"

// Local variable that resides on the stack
typedef struct {
  StrSlice name;
  int depth;
  bool initialized;
  size_t stack_slot;
} Local;

#define T Local
#define TYPE_NAME Locals
#include "dyn_array.h"

typedef struct FnScope {
  Locals locals;
  int depth;
  struct FnScope *enclosing_scope;
} FnScope;

// Stack used for operations.
#define T Value
#define TYPE_NAME Stack
#include "dyn_array.h"

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

Value varmint_go(Varmint *vm, char *source);

#endif
