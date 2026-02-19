#include "error.h"
#include "val.h"

#include <math.h>

#define BINOP_(a, op, b, vat_value_t, vat_return_t) { \
  return value_new( \
    typechecked(vm, a, vat_value_t) op typechecked(vm, b, vat_value_t), \
    vat_return_t \
  ); \
}

#define BINOP(a, op, b, vat_t) BINOP_(a, op, b, vat_t, vat_t)

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
  StringValue *_head = typechecked(vm, head, string),
              *_tail = typechecked(vm, tail, string);

  Str result = str_concat(_head->str, _tail->str);
  return string_value_new(result);
}

Value _vm_in(Varmint *vm, Value x, Value collection)
{
  ValueList *list = typechecked(vm, collection, list);

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

static Value *index_list(Varmint *vm, Value list, Value idx)
{
  ValueList *_list = typechecked(vm, list, list);
  signed long _idx = (signed long)typechecked(vm, idx, number);

  bool index_from_top = _idx < 0;
  size_t idx_magnitude = (size_t)(index_from_top ? -_idx - 1 : _idx);

  if (idx_magnitude >= _list->len)
    runtime_error(vm,
        "List index [%li] out of range (list length %li)\n",
        _idx, _list->len);

  if (index_from_top)
    return ValueList_top(_list) - idx_magnitude;
  else
    return _list->data + _idx;
}

Value _vm_get_elem(Varmint *vm, Value list, Value idx)
{
  return *index_list(vm, list, idx);
}

Value _vm_set_elem(Varmint *vm, Value list, Value idx, Value val)
{
  Value *elem = index_list(vm, list, idx);
  *elem = val;
  return *elem;
}
