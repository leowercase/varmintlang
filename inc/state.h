#ifndef VARMINT_STATE_H
#define VARMINT_STATE_H

#include "code.h"
#include "generic/dyn_array.h"
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
  const uint8_t *ip; // Instruction Ptr
  Procedure *procedure;
  Value *op_stack; // A handle to the call frame's own memory on the stack
  // Closure data.
  size_t upvalue_count; struct Upval **upvalues;
} CallFrame;

// Maximum call depth for functions.
#define CALL_STACK_MAX 1234

typedef DYN_ARRAY_STRUCT(CallFrame) CallStack;
#define T CallFrame
#define ARR CallStack
#include "generic/dyn_array.inc"

typedef struct ArgList {
  size_t argc;
  Value *argv;
} ArgList;

typedef struct {
  Str name;
  Value value;
} NameValue;

typedef DYN_ARRAY_STRUCT(NameValue) NameValues;
#define T NameValue
#define ARR NameValues
#include "generic/dyn_array.inc"

typedef struct Parse Parse;

// Taken from the GNU C library.
#define VARMINT_E 2.7182818284590452354
#define VARMINT_PI 3.14159265358979323846

// https://www.tauday.com/tau-digits
#define VARMINT_TAU (float64_t)6.2831853071795864769252867665590057683943L

#endif
