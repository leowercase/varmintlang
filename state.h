#ifndef LANG_STATE_H
#define LANG_STATE_H

#include "code.h"
#include "generic/dyn_array.h"
#include "generic/table.h"
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

typedef Value (*NativeFn)(struct Varmint *vm, Value *args);

// Native function.
typedef struct Native {
  size_t arity;
  NativeFn fn;
  Str name;
} Native;

typedef DYN_ARRAY_STRUCT(Native) Natives;
#define T Native
#define ARR Natives
#include "generic/dyn_array.inc"

// Maps function names to indices of the natives array.
typedef struct { TABLE_ENTRY(Str, size_t) } NativesTableEntry;
typedef TABLE_STRUCT(NativesTableEntry) NativesTable;
#define K Str
#define V size_t
#define HASH(key) str_hash(key)
#define IS_EMPTY_KEY(key) (key.s == NULL)
#define EMPTY_KEY NULL_STR
#define KEYS_EQ(a, b) strs_eq(a, b)
#define TBL_ENTRY NativesTableEntry
#define TBL NativesTable
#include "generic/table.inc"

#endif
