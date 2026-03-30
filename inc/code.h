#ifndef VARMINT_CODE_H
#define VARMINT_CODE_H

#include "generic/dyn_array.h"
#include "util.h"
#include "val.h"

/*
 * Intermediate representation.
 * https://en.wikipedia.org/wiki/Bytecode
 */

typedef enum {
  OP_NOT = 0,
  OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful op
  OP_UNWRAPPED,

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_POW,
  OP_MODULO,
  OP_AND,
  OP_OR,
  OP_I9N,
  OP_EQ,
  OP_NEQ,
  OP_LT,
  OP_GT,
  OP_LEQ,
  OP_GEQ,
  OP_RANGE,
  OP_RANGE_IN,
  OP_CONCAT,

  OP_CONST,
  OP_CONST16,
  OP_ZERO,
  OP_ONE,
  OP_SWAP,
  OP_SWAP_NEATH,
  OP_SWAP_MOVE_OVER,
  OP_DUP_2,
  OP_BUILD_LIST,
  OP_BUILD_LIST16,
  OP_BUILD_STR,
  OP_BUILD_STR16,
  OP_BUILD_TABLE,
  OP_BUILD_TABLE16,
  OP_EMPTY_TABLE,
  OP_NESTED_TABLE_ENTRIES,
  OP_NESTED_TABLE_ENTRIES16,
  OP_MAKE_SOME,
  OP_MAKE_NONE,
  OP_GET,
  OP_GET16,
  OP_SET,
  OP_SET16,
  OP_GET_UPVALUE,
  OP_GET_UPVALUE16,
  OP_SET_UPVALUE,
  OP_SET_UPVALUE16,
  OP_GET_ELEM,
  OP_SET_ELEM,
  OP_POP,
  OP_RESERVE_SLOT,
  OP_END_BLOCK,
  OP_END_BLOCK16,
  OP_END_EMPTY_BLOCK,
  OP_END_EMPTY_BLOCK16,
  OP_JMP,
  OP_JMP_WHEN_FALSE,
  OP_IF,
  OP_ELSE,
  OP_ELIF,
  OP_LIST_COMPREHEND,
  OP_LOOP,
  OP_LOOP_LIST,
  OP_WHILE,
  OP_WHILE_LIST,
  OP_FOR,
  OP_FOR_LIST,
  OP_FOR_INCREMENT,
  OP_FOR_INCREMENT16,
  OP_BREAK,
  OP_BREAK_LIST,
  OP_DISCARD_FOR,
  OP_DISCARD_FOR_LIST,
  OP_CLOSURE,
  OP_HOIST_UPVALUE,
  OP_CALL,
  OP_CALL16,
  OP_RETURN,
  // Special instruction for GC
  OP_GC,
} Opcode;

// Ensure that all opcodes fit into 8 bits.
static_assert(OP_GC <= UINT8_MAX, "Oops! Too many opcodes.");

static
const Opcode OP_NONE = (Opcode)UINT8_MAX;

typedef DYN_ARRAY_STRUCT(uint8_t) Instructions;
#define T uint8_t
#define ARR Instructions
#define USE_GC
#include "generic/dyn_array.inc"

// The line of text a group of bytes comes from.
typedef struct { size_t line, nbytes; } LineBytes;

typedef DYN_ARRAY_STRUCT(LineBytes) LineInfo;
#define T LineBytes
#define ARR LineInfo
#define USE_GC
#include "generic/dyn_array.inc"

typedef DYN_ARRAY_STRUCT(Value) Constants;
#define T Value
#define ARR Constants
#define USE_GC
#include "generic/dyn_array.inc"

typedef struct {
  Constants constants;
  Instructions instructions;
  LineInfo lines;
} PCode;

typedef struct {
  Str name;
  bool captures_local;
  size_t idx; // Index to local stack slot or the fn's upvalues.
} UpvalDesc;

typedef DYN_ARRAY_STRUCT(UpvalDesc) ClosureDesc;
#define T UpvalDesc
#define ARR ClosureDesc
#define USE_GC
#include "generic/dyn_array.inc"

/*
 * Procedure - a tool for abstraction.
 * Can be a program, can be a function in said program.
 * https://en.wikipedia.org/wiki/Function_(computer_programming)
 */
typedef struct Procedure {
  GCData gc_data;
  Str name;
  size_t arity;
  PCode code;
  ClosureDesc closure_desc;
} Procedure;

/*
 * Functions are by default pure - they can only access and do computation on
 * the parameters they're given. This fits mathematical notions.
 *
 * Sometimes though, there are variables in the function body that aren't
 * arguments or any bindings apparent in the function. These are called
 * "free variables".
 * We need to close any free variables in order to make sense of a computation,
 * so we refer to the function scope above for these "upvalues".
 *
 * https://en.wikipedia.org/wiki/Closure_(computer_programming)
 * https://stackoverflow.com/a/36878651
 * https://mrevelle.blogspot.com/2006/10/closure-on-closures.html
 */
typedef struct Closure {
  GCData gc_data;
  Procedure *procedure;
  size_t upvalue_count;
  struct Upval *upvalues[]; // Flexible array member
} Closure;

// Upvalues can be referenced even after their lifetime ends;
// they're hoisted onto the heap when the scope ends.
typedef struct Upval {
  GCData gc_data;
  Value *loc;
  union {
    struct Upval *next; // Singly linked list of upvalues on the stack
    Value hoisted; // `loc` points to `hoisted` after the value exits the stack.
  };
} Upval;

#endif
