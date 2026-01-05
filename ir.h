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
  OP_CHAIN_BINOP,
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
} PCode;

static inline PCode new_p_code()
{
  PCode p_code;
  p_code.constants = Constants_new();
  p_code.instruc = Instructions_new();
  p_code.lines = LineInfo_new();
  return p_code;
}

void emit_byte(PCode *code, size_t line, uint8_t byte);
void emit_constant(PCode *code, size_t line, Value value);

#endif
