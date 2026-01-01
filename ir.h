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
  OP_CONSTANT,
  OP_CONSTANT_16,
  OP_NOT,
  OP_NEGATE,
  OP_PERCENTAGE,
  OP_FACTORIAL,
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
} Opcode;

#define T uint8_t
#define TYPE_NAME Instructions
#include "dyn_array.h"

// The line some bytes come from.
typedef struct { int nbytes; } LineBytes;

#define T LineBytes
#define TYPE_NAME LineInfo
#include "dyn_array.h"

#define T Value
#define TYPE_NAME Constants
#include "dyn_array.h"

// ...I'm not abusing "dyn_array.h" at all :)

typedef struct {
  Constants constants;
  Instructions instruc;
  LineInfo lines;
} PCode;

inline PCode new_p_code()
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
