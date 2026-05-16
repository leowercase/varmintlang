#ifndef VARMINT_CODE_H
#define VARMINT_CODE_H

#include "generic/dyn_array.h"
#include "util.h"
#include "val.h"

/*
 * Bytecode.
 * https://en.wikipedia.org/wiki/Bytecode
 */

// Some operations have a variably sized operand that gets more space only
// when required.
// This makes more common smaller-operand instructions slightly faster overall.
#define var(op) op, op##_16

typedef enum {
  OP_NOT = 0,
  OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful op

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_POW,
  OP_MODULO,
  OP_AND,
  OP_OR,
  OP_IMPLIES,
  OP_EQ,
  OP_NEQ,
  OP_LT,
  OP_GT,
  OP_LEQ,
  OP_GEQ,
  OP_CONCAT,
  OP_NCAT,

  var(OP_CONST),

  var(OP_BUILD_LIST),
  var(OP_BUILD_STR),
  var(OP_BUILD_TABLE),
  var(OP_NESTED_TABLE_ENTRIES),
  OP_MAKE_SOME,
  OP_MAKE_NONE,

  var(OP_GET),
  var(OP_SET),
  var(OP_GET_UPVALUE),
  var(OP_SET_UPVALUE),
  OP_GET_ELEM,
  OP_SET_ELEM,
  OP_MAYBE_GET_ELEM,

  OP_POP,
  OP_SWAP,
  OP_SWAP_NEATH,
  OP_SWAP_MOVE_OVER,
  OP_DUP_2,
  var(OP_MIRROR),

  OP_RESERVE_SLOT,
  var(OP_END_BLOCK),
  var(OP_LEVEL_BLOCK),
  OP_END_SLOTS,

  OP_JMP,
  OP_JMP_WHEN_FALSE,

  OP_IF,
  OP_ELSE,
  OP_ELIF,

  OP_LOOP,
  OP_FOR,
  OP_FOR_JMP,
  OP_FOR_DISCARD,

  OP_CLOSURE,
  var(OP_HOIST),
  var(OP_PARTIAL),
  var(OP_CALL),
  OP_CALL_UNARY,

  OP_RETURN,
  OP_SUSPEND,
  OP_HALT,

  OP_RESUME,

  // Special instruction for GC
  OP_GC,
} Opcode;

#undef var

// Ensure that all opcodes fit into 8 bits.
static_assert(OP_GC <= UINT8_MAX, "Oops! Too many opcodes.");

static
const Opcode OP_NONE = (Opcode)(UINT8_MAX + 1);

typedef DYN_ARRAY_STRUCT(uint8_t) Instructions;
#define T uint8_t
#define ARR Instructions
#define USE_GC
#include "generic/dyn_array.inc"

// The source code line a group of bytes comes from.
typedef struct { size_t line, bytes; } LineBytes;

// Bytecode counts on each line are kept track of
// for debugging and disassembly purposes
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
  String *source;
  ClosureDesc closure_desc;
} Procedure;

/*
 * Functions are by default pure - they can only access and do computation on
 * the parameters they're given. This fits mathematical notions.
 *
 * Sometimes though, there are variables in the function body that aren't
 * arguments or any bindings apparent in the function.
 * Any free variables need to be closed in order to make sense of a computation
 * and we refer to the enclosing function for these "upvalues".
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

// Upvalues can be referenced even after their lifetime on the stack ends;
// they're hoisted onto the heap on scope exit.
typedef struct Upval {
  GCData gc_data;
  Value *loc;
  union {
    struct Upval *next; // Singly linked list of upvalues on the stack
    Value hoisted; // `loc` points to `hoisted` after the value exits the stack.
  };
} Upval;

#endif
