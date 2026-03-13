#ifndef LANG_OP_H
#define LANG_OP_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Enum definitions for native operators.
 */

// UnOp / BinOp / OP_NONE
typedef int Op;

static
const Op OP_NONE = -1;

typedef enum {
  OP_NOT = 0,
  OP_NEGATE,
  OP_FACTORIAL,
  OP_PERCENTAGE, // 100% a useful op
} UnOp;

typedef enum {
  OP_ADD = OP_PERCENTAGE + 1,
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
  OP_IN,
  OP_NOTIN,
  OP_RANGE,
  OP_RANGE_IN,
  OP_CONCAT,
} BinOp;

static inline
bool is_unary_op(Op op)
{
  return OP_NOT <= op && op <= OP_PERCENTAGE;
}

static inline
bool is_binary_op(Op op)
{
  return OP_ADD <= op && op <= OP_CONCAT;
}

#define NATIVE_OPERATOR_COUNT ((int)OP_CONCAT + 1)

#endif
