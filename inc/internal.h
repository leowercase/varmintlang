#ifndef VARMINT_INTERNAL_H
#define VARMINT_INTERNAL_H

#include "val.h"
#include "varmint.h"

/* INTERNAL OPERATIONS */

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
Value _vm_ncat(Varmint *vm, Value cattee, Value n);

Value _vm_in(Varmint *vm, Value x, Value collection);

Value _vm_get_elem(Varmint *vm, Value collection, Value idx, bool wrap_maybe);
Value _vm_set_elem(Varmint *vm, Value collection, Value idx, Value val);

/* INTERNAL FUNCTIONS */

// typeof(val) -> string
Value _typeof(Varmint *vm, ArgList *args);
// len(collection) -> number
Value _len(Varmint *vm, ArgList *args);

// abs(a: number) -> number
Value _abs(Varmint *vm, ArgList *args);

// sqrt(n: number) -> number
Value _sqrt(Varmint *vm, ArgList *args);
// cbrt(n: number) -> number
Value _cbrt(Varmint *vm, ArgList *args);

// ln(x: number) -> number
Value _ln(Varmint *vm, ArgList *args);
// lg(x: number) -> number
Value _lg(Varmint *vm, ArgList *args);

// sin(theta: number) -> number
Value _sin(Varmint *vm, ArgList *args);
// cos(theta: number) -> number
Value _cos(Varmint *vm, ArgList *args);
// tan(theta: number) -> number
Value _tan(Varmint *vm, ArgList *args);

// asin(x: number) -> number
Value _asin(Varmint *vm, ArgList *args);
// acos(x: number) -> number
Value _acos(Varmint *vm, ArgList *args);
// atan(x: number) -> number
Value _atan(Varmint *vm, ArgList *args);

// rand() -> number
Value _rand(Varmint *vm, ArgList *args);

// push(list, val) -> ?
Value _push(Varmint *vm, ArgList *args);
// pop(list) -> ?
Value _pop(Varmint *vm, ArgList *args);

// to_number(val) -> maybe(number)
Value _to_number(Varmint *vm, ArgList *args);
// to_string(val) -> string
Value _to_string(Varmint *vm, ArgList *args);
// unwrap(val: maybe) -> ?
Value _unwrap(Varmint *vm, ArgList *args);

// has(collection, elem)
Value _has(Varmint *vm, ArgList *args);

// put(output: string)
Value _put(Varmint *vm, ArgList *args);
// putln(output: string)
Value _putln(Varmint *vm, ArgList *args);
// input(prompt: string) -> string
Value _input(Varmint *vm, ArgList *args);

// time() -> number
Value _time(Varmint *vm, ArgList *args);

// range(start: number, end: number, step: number) -> cclosure
Value _range(Varmint *vm, ArgList *args);
// items(collection) -> cclosure
Value _items(Varmint *vm, ArgList *args);

// asciify(n: number) -> string
Value _asciify(Varmint *vm, ArgList *args);
// char_ord(char: string) -> number
Value _char_ord(Varmint *vm, ArgList *args);

// rot(shift: number, text: string) -> string
Value _rot(Varmint *vm, ArgList *args);

#endif
