#ifndef LANG_VM_H
#define LANG_VM_H

#include "ir.h"
#include "util.h"
#include "val.h"

#define T Value
#define TYPE_NAME Stack
#include "dyn_array.h"

typedef struct {
  uint8_t *ip;
  Stack stack;

  Value cmp_rhs;
} VM;

VM vm_new();
Value vm_run(VM *vm, PCode *code);
void vm_free(VM *vm);

#endif
