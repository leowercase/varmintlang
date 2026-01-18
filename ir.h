#ifndef LANG_IR_H
#define LANG_IR_H

#include "dyn_array_header.h"
#include "util.h"
#include "val.h"

/*
 * Intermediate representation.
 * https://en.wikipedia.org/wiki/Bytecode
 */

typedef enum {
  OP_NONE,
  OP_CONST,
  OP_CONST16,
  OP_NOT,
  OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful instruction
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
  OP_BUILD_LIST,
  OP_BUILD_LIST16,
  OP_TO_STR,
  OP_CONCAT,
  OP_BUILD_STR,
  OP_BUILD_STR16,
  OP_CHAIN_BINOP,
  OP_GET,
  OP_GET16,
  OP_SET,
  OP_SET16,
  OP_RESERVE_SLOT,
  OP_DISCARD,
  OP_DISCARDN,
  OP_DISCARDN16,
  OP_RETAIN1_DISCARDN,
  OP_RETAIN1_DISCARDN16,
  OP_RETURN,
} Opcode;

static_assert(OP_RETURN <= UINT8_MAX, "Oops! Too many opcodes.");

typedef DYN_ARRAY_STRUCT(uint8_t) Instructions;

#define T uint8_t
#define TYPE_NAME Instructions
#include "dyn_array.h"

// The line of text a group of bytes come from.
typedef struct { size_t line, nbytes; } LineBytes;

typedef DYN_ARRAY_STRUCT(LineBytes) LineInfo;

#define T LineBytes
#define TYPE_NAME LineInfo
#include "dyn_array.h"

/*
 * Lines are run-length encoded to save memory.
 * This makes line info a bit slow to emit, but it only happens on errors.
 * https://en.wikipedia.org/wiki/Run-length_encoding
 */
size_t get_line(LineInfo *l, size_t instruction_idx);

typedef DYN_ARRAY_STRUCT(Value) Constants;

#define T Value
#define TYPE_NAME Constants
#include "dyn_array.h"

typedef struct {
  Constants constants;
  Instructions instruc;
  LineInfo lines;
  size_t variable_count;
} PCode;

static inline PCode new_p_code()
{
  PCode p_code;
  p_code.constants = Constants_new();
  p_code.instruc = Instructions_new();
  p_code.lines = LineInfo_new();
  return p_code;
}

// Record a byte into code.
void emit_byte(PCode *code, size_t line, uint8_t byte);

// For brevity.
#define emit_bytes(code, line, n, ...) do { \
  uint8_t b[] = {__VA_ARGS__}; \
  for (int i = 0; i < (n); i++) \
    emit_byte((code), (line), b[i]); \
} while (false)

// Returns an instruction pointer to the (16-bit) operand.
uint8_t *defer_operand(PCode *code, size_t line);
// Inserts operand of defer_operand into the code
void patch_operand(PCode *code, uint8_t *ip, uint16_t operand);

// Maximum size of variable sized operands
static const size_t MAX_OPERAND_SIZE = UINT16_MAX;

// Emit an operation that has a variable sized size operand
bool emit_size_op(PCode *code, size_t line, Opcode opcode, size_t size);

// Emit a code constant.
void emit_constant(PCode *code, size_t line, Value value);

// Local variable that resides on the stack
typedef struct {
  StrSlice name;
  int depth;
  bool initialized;
  size_t stack_slot;
} Local;

typedef DYN_ARRAY_STRUCT(Local) Locals;

#define T Local
#define TYPE_NAME Locals
#include "dyn_array.h"

// Scope of a function
typedef struct FnScope {
  Locals locals;
  int depth;
  struct FnScope *enclosing_scope;
} FnScope;

// Stack used for operations.
typedef DYN_ARRAY_STRUCT(Value) Stack;

#define T Value
#define TYPE_NAME Stack
#include "dyn_array.h"

#endif
