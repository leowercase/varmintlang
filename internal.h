#ifndef LANG_INTERNAL_H
#define LANG_INTERNAL_H

#include "val.h"

Value _vm_not(Value P);
Value _vm_negate(Value invertee);

Value _vm_factorial(Value n);
Value _vm_percentage(Value p);

Value _vm_add(Value augend, Value addend);
Value _vm_subtract(Value subtrahend, Value minuend);
Value _vm_multiply(Value multiplier, Value multiplicand);
Value _vm_divide(Value dividend, Value divisor);

Value _vm_pow(Value base, Value power);
Value _vm_modulo(Value a, Value n);

Value _vm_and(Value P, Value Q);
Value _vm_or(Value P, Value Q);
Value _vm_implies(Value P, Value Q);

Value _vm_less_than(Value a, Value b);
Value _vm_less_than_or_eq(Value a, Value b);
Value _vm_greater_than(Value a, Value b);
Value _vm_greater_than_or_eq(Value a, Value b);

Value _vm_concat(Value head, Value tail);

Value _vm_in(Value x, Value collection);
Value _vm_notin(Value x, Value collection);

Value _vm_get_elem(Value list, Value idx);
Value _vm_set_elem(Value list, Value idx, Value val);

#endif
