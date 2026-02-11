#ifndef LANG_STATE_H
#define LANG_STATE_H

#include "generic/dyn_array.h"
#include "proc.h"
#include "val.h"

// Maximum op stack size, fairly arbitrarily picked.
// https://oeis.org/A000079
#define OP_STACK_MAX 16384

/*
 * Stack used for operations.
 *
 * A static operation stack that lives on the C stack is an important invariant.
 * If the stack were to be reallocated during a resize, all call frames' handles
 * to stack slots would suddenly point to invalid memory, which is bad.
 */
typedef CAPPED_DYN_ARRAY_STRUCT(Value, OP_STACK_MAX) OpStack;
#define T Value
#define ARR OpStack
#define CAP OP_STACK_MAX
#include "generic/dyn_array.inc"

/*
 * Contains all memory required by a procedure on runtime.
 * https://www.youtube.com/watch?v=aCPkszeKRa4
 */
typedef struct {
  uint8_t *ip; // Instruction Ptr
  Proc *procedure;
  Value *op_stack; // a handle to the call frame's own memory on the stack
} CallFrame;

typedef DYN_ARRAY_STRUCT(CallFrame) CallStack;
#define T CallFrame
#define ARR CallStack
#include "generic/dyn_array.inc"

#endif
