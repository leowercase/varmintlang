#ifndef LANG_INTERNAL_H
#define LANG_INTERNAL_H

#include "val.h"
#include "varmint.h"

Value _vm_not(Varmint *vm, Value P);
Value _vm_negate(Varmint *vm, Value invertee);

Value _vm_factorial(Varmint *vm, Value n);
Value _vm_percentage(Varmint *vm, Value p);

Value _vm_add(Varmint *vm, Value augend, Value addend);
Value _vm_subtract(Varmint *vm, Value subtrahend, Value minuend);
Value _vm_multiply(Varmint *vm, Value multiplier, Value multiplicand);
Value _vm_divide(Varmint *vm, Value dividend, Value divisor);

Value _vm_pow(Varmint *vm, Value base, Value power);
Value _vm_modulo(Varmint *vm, Value a, Value n);

Value _vm_and(Varmint *vm, Value P, Value Q);
Value _vm_or(Varmint *vm, Value P, Value Q);
Value _vm_implies(Varmint *vm, Value P, Value Q);

Value _vm_less_than(Varmint *vm, Value a, Value b);
Value _vm_less_than_or_eq(Varmint *vm, Value a, Value b);
Value _vm_greater_than(Varmint *vm, Value a, Value b);
Value _vm_greater_than_or_eq(Varmint *vm, Value a, Value b);

Value _vm_concat(Varmint *vm, Value head, Value tail);

Value _vm_in(Varmint *vm, Value x, Value collection);
Value _vm_notin(Varmint *vm, Value x, Value collection);

Value _vm_get_elem(Varmint *vm, Value list, Value idx);
Value _vm_set_elem(Varmint *vm, Value list, Value idx, Value val);

#endif
