#ifndef LANG_OP_H
#define LANG_OP_H

#include <stddef.h>

/*
 * Enum definitions for native operators.
 */

// UnOp / BinOp / OP_NONE
typedef unsigned int Op;

typedef enum {
  OP_NOT = 0,
  OP_POSITE, OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful op
} UnOp;

static
const size_t UNOP_COUNT = (int)OP_PERCENTAGE + 1;

typedef enum {
  OP_ADD = UNOP_COUNT,
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
  OP_CONCAT,
} BinOp;

static
const size_t NATIVE_OPERATOR_COUNT = (int)OP_CONCAT + 1;

static
const size_t OP_NONE = NATIVE_OPERATOR_COUNT;

#endif
