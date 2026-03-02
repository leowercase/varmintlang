#include "error.h"
#include "val.h"

#include <math.h>

#define BINOP_(lhs, op, rhs, vm_value_t, vm_return_t) { \
  return value_new( \
    typechecked(vm, lhs, vm_value_t) op typechecked(vm, rhs, vm_value_t), \
    vm_return_t \
  ); \
}

#define BINOP(lhs, op, rhs, vm_t) BINOP_(lhs, op, rhs, vm_t, vm_t)

Value _vm_not(Varmint *vm, Value P)
{
  return value_new(!typechecked(vm, P, boolean), boolean);
}

Value _vm_negate(Varmint *vm, Value invertee)
{
  return value_new(-typechecked(vm, invertee, number), number);
}

Value _vm_factorial(Varmint *vm, Value n)
{
  float64_t _n = typechecked(vm, n, number);

  float64_t f = 1;
  for (int i = 0; i < _n; i++) f *= i;

  return value_new(f, number);
}

Value _vm_percentage(Varmint *vm, Value p)
{
  return value_new(typechecked(vm, p, number) * 0.01, number);
}

Value _vm_add(Varmint *vm, Value augend, Value addend)
  BINOP(augend, +, addend, number)

Value _vm_subtract(Varmint *vm, Value subtrahend, Value minuend)
  BINOP(subtrahend, -, minuend, number)

Value _vm_multiply(Varmint *vm, Value multiplier, Value multiplicand)
    /*  Markiplier  */
  BINOP(multiplier, *, multiplicand, number)

Value _vm_divide(Varmint *vm, Value dividend, Value divisor)
  BINOP(dividend, /, divisor, number)

Value _vm_pow(Varmint *vm, Value base, Value power)
{
  float64_t _base = typechecked(vm, base, number),
            _power = typechecked(vm, power, number);

  return value_new(pow(_base, _power), number);
}

Value _vm_modulo(Varmint *vm, Value a, Value n)
{
  float64_t _a = typechecked(vm, a, number),
            _n = typechecked(vm, n, number);

  return value_new(fmod(_a, _n), number);
}

Value _vm_and(Varmint *vm, Value P, Value Q)
  BINOP(P, &&, Q, boolean)

Value _vm_or(Varmint *vm, Value P, Value Q)
  BINOP(P, ||, Q, boolean)

Value _vm_implies(Varmint *vm, Value P, Value Q)
{
  bool _P = typechecked(vm, P, boolean),
       _Q = typechecked(vm, Q, boolean);

  return value_new(!_P || _Q, boolean);
}

Value _vm_less_than(Varmint *vm, Value a, Value b)
  BINOP_(a, <, b, number, boolean)

Value _vm_less_than_or_eq(Varmint *vm, Value a, Value b)
  BINOP_(a, <=, b, number, boolean)

Value _vm_greater_than(Varmint *vm, Value a, Value b)
  BINOP_(a, >, b, number, boolean)

Value _vm_greater_than_or_eq(Varmint *vm, Value a, Value b)
  BINOP_(a, >=, b, number, boolean)

Value _vm_concat(Varmint *vm, Value head, Value tail)
{
  if (head.type != V_string || tail.type != V_string)
    runtime_error(vm, "invalid operand types to concatenation\n");

  return *String_concat(vm, &head, &tail);
}

Value _vm_in(Varmint *vm, Value x, Value collection)
{
  List *list = typechecked(vm, collection, list);

  for (size_t i = 0; i < list->len; i++) {
    Value elem = list->data[i];

    if (values_eq(x, elem))
      return value_new(true, boolean);
  }

  return value_new(false, boolean);
}

Value _vm_notin(Varmint *vm, Value x, Value collection)
{
  return value_new(
      value_is_falsey(_vm_in(vm, x, collection)), boolean);
}

static size_t index_into(Varmint *vm, size_t len, Value idx)
{
  float64_t _idx = typechecked(vm, idx, number);

  size_t actual_idx;
  if (_idx < 0)
    // Index from top.
    actual_idx = (size_t)((float64_t)len + _idx);
  else
    actual_idx = (size_t)_idx;

  if (actual_idx >= len)
    runtime_error(vm, "list index [%li] out of range (length %li)\n",
        _idx, len);

  return actual_idx;
}

static inline Value *index_list(Varmint *vm, Value list, Value idx)
{
  List *_list = typechecked(vm, list, list);
  return &_list->data[index_into(vm, _list->len, idx)];
}

Value _vm_get_elem(Varmint *vm, Value collection, Value idx)
{
  switch (collection.type) {
  case V_string:
    {
      String *string = collection.as.string;
      char c = string->s[index_into(vm, string->len, idx)];
      return *String_create(vm, &c, 1);
    }
  case V_list:
    return *index_list(vm, collection, idx);
  default:
    runtime_error(vm, "cannot index into %s\n",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

static Value set_string_idx(Varmint *vm, String *string, Value idx, Value val)
{
  if (val.type != V_string || val.as.string->len - 1 != 1)
    runtime_error(vm, "string index assignment must be a single character\n");

  string->s[index_into(vm, string->len, idx)] = val.as.string->s[0];
  return val;
}

Value _vm_set_elem(Varmint *vm, Value collection, Value idx, Value val)
{
  if (collection.type == V_string)
    return set_string_idx(vm, collection.as.string, idx, val);

  else {
    Value *elem = index_list(vm, collection, idx);
    *elem = val;
    return *elem;
  }
}
