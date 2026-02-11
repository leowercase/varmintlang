#include "val.h"

#include <math.h>

#define BINOP_(a, op, b, vat_value_t, vat_return_t) { \
  return value_new( \
    typechecked(a, vat_value_t) op typechecked(b, vat_value_t), \
    vat_return_t \
  ); \
}

#define BINOP(a, op, b, vat_t) BINOP_(a, op, b, vat_t, vat_t)

Value _vm_not(Value P)
{
  return value_new(!typechecked(P, boolean), boolean);
}

Value _vm_negate(Value invertee)
{
  return value_new(-typechecked(invertee, number), number);
}

Value _vm_factorial(Value n)
{
  float64_t _n = typechecked(n, number);

  float64_t f = 1;
  for (int i = 0; i < _n; i++) f *= i;

  return value_new(f, number);
}

Value _vm_percentage(Value p)
{
  return value_new(typechecked(p, number) * 0.01, number);
}

Value _vm_add(Value augend, Value addend)
  BINOP(augend, +, addend, number)

Value _vm_subtract(Value subtrahend, Value minuend)
  BINOP(subtrahend, -, minuend, number)

Value _vm_multiply(Value multiplier, Value multiplicand)
  BINOP(multiplier, *, multiplicand, number)
    /*  Markiplier  */

Value _vm_divide(Value dividend, Value divisor)
  BINOP(dividend, /, divisor, number)

Value _vm_pow(Value base, Value power)
{
  float64_t _base = typechecked(base, number),
            _power = typechecked(power, number);

  return value_new(pow(_base, _power), number);
}

Value _vm_modulo(Value a, Value n)
{
  float64_t _a = typechecked(a, number),
            _n = typechecked(n, number);

  return value_new(fmod(_a, _n), number);
}

Value _vm_and(Value P, Value Q)
  BINOP(P, &&, Q, boolean)

Value _vm_or(Value P, Value Q)
  BINOP(P, ||, Q, boolean)

Value _vm_implies(Value P, Value Q)
{
  bool _P = typechecked(P, boolean),
       _Q = typechecked(Q, boolean);

  return value_new(!_P || _Q, boolean);
}

Value _vm_less_than(Value a, Value b)
  BINOP_(a, <, b, number, boolean)

Value _vm_less_than_or_eq(Value a, Value b)
  BINOP_(a, <=, b, number, boolean)

Value _vm_greater_than(Value a, Value b)
  BINOP_(a, >, b, number, boolean)

Value _vm_greater_than_or_eq(Value a, Value b)
  BINOP_(a, >=, b, number, boolean)

Value _vm_concat(Value head, Value tail)
{
  StringValue *_head = typechecked(head, string),
              *_tail = typechecked(tail, string);

  Str result = str_concat(_head->str, _tail->str);
  return string_value_new(result);
}

Value _vm_in(Value x, Value collection)
{
  ValueList *list = typechecked(collection, list);

  for (size_t i = 0; i < list->len; i++) {
    Value elem = list->data[i];

    if (values_eq(x, elem))
      return value_new(true, boolean);
  }

  return value_new(false, boolean);
}

Value _vm_notin(Value x, Value collection)
{
  return value_new(
      value_is_falsey(_vm_in(x, collection)), boolean);
}

static Value *list_idx(Value list, Value idx)
{
  ValueList *_list = typechecked(list, list);
  signed long _idx = (signed long)typechecked(idx, number);

  bool index_from_top = _idx < 0;
  size_t idx_magnitude = (size_t)(index_from_top ? -_idx - 1 : _idx);

  if (idx_magnitude >= _list->len)
    runtime_error(
        "List index [%li] out of range (list length %li)\n",
        _idx, _list->len);

  if (index_from_top)
    return ValueList_top(_list) - idx_magnitude;
  else
    return _list->data + _idx;
}

Value _vm_get_elem(Value list, Value idx)
{
  return *list_idx(list, idx);
}

Value _vm_set_elem(Value list, Value idx, Value val)
{
  Value *elem = list_idx(list, idx);
  *elem = val;
  return *elem;
}
