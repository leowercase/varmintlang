#ifndef LANG_INTERNAL_H
#define LANG_INTERNAL_H

#include "val.h"

Value __vat_not(Value P);
Value __vat_negate(Value invertee);

Value __vat_factorial(Value n);
Value __vat_percentage(Value p);

Value __vat_add(Value augend, Value addend);
Value __vat_subtract(Value subtrahend, Value minuend);
Value __vat_multiply(Value multiplier, Value multiplicand);
Value __vat_divide(Value dividend, Value divisor);

Value __vat_pow(Value base, Value power);
Value __vat_modulo(Value a, Value n);

Value __vat_and(Value P, Value Q);
Value __vat_or(Value P, Value Q);
Value __vat_implies(Value P, Value Q);

Value __vat_less_than(Value a, Value b);
Value __vat_less_than_or_eq(Value a, Value b);
Value __vat_greater_than(Value a, Value b);
Value __vat_greater_than_or_eq(Value a, Value b);

#endif
