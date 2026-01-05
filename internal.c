#include "val.h"

#include <math.h>

#define BINOP_(a, op, b, vat_value_t, vat_return_t) { \
  return value_new( \
    typechecked(a, vat_value_t) op typechecked(b, vat_value_t), \
    vat_return_t \
  ); \
}

#define BINOP(a, op, b, vat_t) BINOP_(a, op, b, vat_t, vat_t)

Value __vat_not(Value P)
{
  return value_new(!typechecked(P, boolean), boolean);
}

Value __vat_negate(Value invertee)
{
  return value_new(-typechecked(invertee, number), number);
}

Value __vat_factorial(Value n)
{
  float64_t n_raw = typechecked(n, number);

  float64_t f = 1;
  for (int i = 0; i < n_raw; i++) f *= i;

  return value_new(f, number);
}

Value __vat_percentage(Value p)
{
  return value_new(typechecked(p, number) * 0.01, number);
}

Value __vat_add(Value augend, Value addend)
  BINOP(augend, +, addend, number)

Value __vat_subtract(Value subtrahend, Value minuend)
  BINOP(subtrahend, -, minuend, number)

Value __vat_multiply(Value multiplier, Value multiplicand)
  BINOP(multiplier, *, multiplicand, number)

Value __vat_divide(Value dividend, Value divisor)
  BINOP(dividend, /, divisor, number)

Value __vat_pow(Value base, Value power)
{
  float64_t b = typechecked(base, number),
            n = typechecked(power, number);

  return value_new(pow(b, n), number);
}

Value __vat_modulo(Value a, Value n)
{
  float64_t a_raw = typechecked(a, number),
            n_raw = typechecked(n, number);

  return value_new(fmod(a_raw, n_raw), number);
}

Value __vat_and(Value P, Value Q)
  BINOP(P, &&, Q, boolean)

Value __vat_or(Value P, Value Q)
  BINOP(P, ||, Q, boolean)

Value __vat_implies(Value P, Value Q)
{
  bool p = typechecked(P, boolean),
       q = typechecked(Q, boolean);

  return value_new(!p || q, boolean);
}

Value __vat_less_than(Value a, Value b)
  BINOP_(a, <, b, number, boolean)

Value __vat_less_than_or_eq(Value a, Value b)
{
  float64_t a_raw = typechecked(a, number),
            b_raw = typechecked(a, number);

  return value_new(a_raw < b_raw || a_raw == b_raw, number);
}

Value __vat_greater_than(Value a, Value b)
  BINOP_(a, >, b, number, boolean)

Value __vat_greater_than_or_eq(Value a, Value b)
{
  float64_t a_raw = typechecked(a, number),
            b_raw = typechecked(a, number);

  return value_new(a_raw > b_raw || a_raw == b_raw, number);
}
