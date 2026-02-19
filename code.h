#ifndef LANG_CODE_H
#define LANG_CODE_H

#include "generic/dyn_array.h"
#include "util.h"
#include "val.h"
#include "op.h"

/*
 * Intermediate representation.
 * https://en.wikipedia.org/wiki/Bytecode
 */

typedef enum {
/*
  OP_NOT, ...,
  OP_ADD, ..., */
  OP_CONST = NATIVE_OPERATOR_COUNT,
  OP_CONST16,
  OP_ONE,
  OP_BUILD_LIST,
  OP_BUILD_LIST16,
  OP_BUILD_STR,
  OP_BUILD_STR16,
  OP_CHAIN_BINOP,
  OP_GET,
  OP_GET16,
  OP_SET,
  OP_SET16,
  OP_LIST_GET,
  OP_LIST_SET,
  OP_RESERVE_SLOT,
  OP_DUPLICATE,
  OP_DISCARD,
  OP_DISCARDN,
  OP_DISCARDN16,
  OP_RETAIN1_DISCARDN,
  OP_RETAIN1_DISCARDN16,
  OP_JMP,
  OP_JMP_IFFEN,
  OP_CALL,
  OP_CALL16,
  OP_RETURN,
} Opcode;

static_assert(OP_RETURN <= UINT8_MAX, "Oops! Too many opcodes.");

typedef DYN_ARRAY_STRUCT(uint8_t) Instructions;
#define T uint8_t
#define ARR Instructions
#include "generic/dyn_array.inc"

// The line of text a group of bytes come from.
typedef struct { size_t line, nbytes; } LineBytes;

typedef DYN_ARRAY_STRUCT(LineBytes) LineInfo;

#define T LineBytes
#define ARR LineInfo
#include "generic/dyn_array.inc"

/*
 * Lines are run-length encoded to save memory.
 * This makes line info a bit slow to emit, but it only happens on errors.
 * https://en.wikipedia.org/wiki/Run-length_encoding
 */
size_t get_line(LineInfo *l, size_t instruction_idx);

typedef DYN_ARRAY_STRUCT(Value) Constants;

#define T Value
#define ARR Constants
#include "generic/dyn_array.inc"

typedef struct {
  Constants constants;
  Instructions instructions;
  LineInfo lines;
} PCode;

static inline PCode new_p_code(void)
{
  PCode p_code;
  p_code.constants = Constants_new();
  p_code.instructions = Instructions_new();
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
uint8_t *defer_op(PCode *code, size_t line, Opcode op);
// Inserts operand of defer_op into code
void patch_op(PCode *code, uint8_t *ip, uint16_t operand);

// Maximum size of variable sized operands
static const size_t MAX_OPERAND_SIZE = UINT16_MAX;

// Emit an operation that has a variable sized size operand
bool emit_size_op(PCode *code, size_t line, Opcode opcode, size_t size);

#endif
