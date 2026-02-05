#ifndef LANG_STATE_H
#define LANG_STATE_H

#include "generic/dyn_array.h"
#include "proc.h"
#include "val.h"

// Stack used for operations.
typedef DYN_ARRAY_STRUCT(Value) OpStack;
#define T Value
#define ARR OpStack
#include "generic/dyn_array.inc"

/*
 * Contains all memory required by a subprocess on runtime.
 * https://www.youtube.com/watch?v=aCPkszeKRa4
 */
typedef struct {
  uint8_t *ip; // Instruction Ptr
  Proc *proc;
  Value *op_stack_slot; // Position in the op stack
} CallFrame;

typedef DYN_ARRAY_STRUCT(CallFrame) CallStack;
#define T CallFrame
#define ARR CallStack
#include "generic/dyn_array.inc"

#endif
