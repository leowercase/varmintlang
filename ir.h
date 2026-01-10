#ifndef LANG_IR_H
#define LANG_IR_H

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
  OP_DISCARD,
  OP_RETURN,
} Opcode;

#define T uint8_t
#define TYPE_NAME Instructions
#include "dyn_array.h"

// The line of text a group of bytes come from.
typedef struct { size_t line, nbytes; } LineBytes;

#define T LineBytes
#define TYPE_NAME LineInfo
#include "dyn_array.h"

/*
 * Lines are run-length encoded to save memory.
 * This makes line info a bit slow to emit, but it only happens on errors.
 * https://en.wikipedia.org/wiki/Run-length_encoding
 */
size_t get_line(LineInfo *l, size_t instruction_idx);

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

// Emit an operation that has a variable sized size operand
bool emit_size(PCode *code, size_t line, Opcode opcode, size_t size);

// Emit a code constant.
void emit_constant(PCode *code, size_t line, Value value);

#endif
