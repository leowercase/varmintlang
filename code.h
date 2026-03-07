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
  OP_ZERO,
  OP_ONE,
  OP_CHAIN_BINOP,
  OP_BUILD_LIST,
  OP_BUILD_LIST16,
  OP_BUILD_STR,
  OP_BUILD_STR16,
  OP_MAKE_SOME,
  OP_MAKE_NONE,
  OP_GET,
  OP_GET16,
  OP_SET,
  OP_SET16,
  OP_INDEXED_GET,
  OP_INDEXED_SET,
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
  OP_CALL,
  OP_CALL16,
  OP_RETURN,
  // Special instruction for GC
  OP_GC,
} Opcode;

static_assert(OP_GC <= UINT8_MAX, "Oops! Too many opcodes.");

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

static inline
PCode new_p_code(void)
{
  PCode code;
  code.constants = Constants_init();
  code.instructions = Instructions_init();
  code.lines = LineInfo_init();
  return code;
}

static inline
void free_p_code(PCode *code)
{
  free(code->constants.data);
  free(code->instructions.data);
  free(code->lines.data);
}

// Record a byte into code.
void emit_byte(PCode *code, size_t line, uint8_t byte);

// For brevity.
#define emit_bytes(code, line, n, ...) do { \
  uint8_t b[] = {__VA_ARGS__}; \
  for (int i = 0; i < (n); i++) \
    emit_byte((code), (line), b[i]); \
} while (false)

// Returns the index of the (16-bit) operand in the code chunk.
size_t defer_op(PCode *code, size_t line, Opcode op);
// Inserts operand of defer_op into code
void patch_op(PCode *code, size_t operand_idx, uint16_t operand);

// Emit an operation that has a variable sized operand
bool emit_var_op(PCode *code, size_t line, Opcode opcode, size_t operand);

#endif
