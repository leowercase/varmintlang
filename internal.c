#include "info.h"
#include "val.h"

#include <ctype.h>
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

Value _vm_range(Varmint *vm, Value left, Value right, bool inclusive)
{
  float64_t a = typechecked(vm, left, number),
            b = typechecked(vm, right, number);
  return Range_create(vm, a, b, inclusive);
}

Value _vm_concat(Varmint *vm, Value head, Value tail)
{
  if (head.type != V_string || tail.type != V_string)
    runtime_error(vm, "invalid operand types to concatenation\n");

  return String_concat(vm, &head, &tail);
}

Value _vm_in(Varmint *vm, Value x, Value collection)
{
  bool contains = false;

  switch (collection.type) {
  case V_range:
    {
      Range *range = collection.as.range;
      float64_t n = typechecked(vm, x, number);
      contains =
        range->inclusive
          ? range->start <= n && n <= range->end
          : range->start <= n && n < range->end;
      break;
    }
  case V_list:
    for (size_t i = 0; i < collection.as.list->len; i++) {
      Value elem = collection.as.list->data[i];
      if (values_eq(x, elem)) {
        contains = true; break;
      }
    }
    break;
  case V_string:
    {
      String *string = collection.as.string;
      String *substring = typechecked(vm, x, string);

      if (substring->len == 0 || substring->len > string->len) {
        contains = false; break;
      }

      for (size_t i = 0; string->len - i >= substring->len; i++) {
        char *c = &string->s[i];

        if (*c == substring->s[0]
            && strncmp(&c[1], &substring->s[1], substring->len - 1) == 0) {
          contains = true; break;
        }
      }
      break;
    }
  default:
    runtime_error(vm, "`in`: expect collection, got %s\n",
        value_type_cstring(collection.type));
  }

  return value_new(contains, boolean);
}

Value _vm_notin(Varmint *vm, Value x, Value collection)
{
  return value_new(
      value_is_falsey(_vm_in(vm, x, collection)), boolean);
}

// Allow indexing from the top with negative numbers.
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
    runtime_error(vm, "index [%li] out of range (length %li)\n", _idx, len);

  return actual_idx;
}

static inline Value *index_list(Varmint *vm, List *list, Value idx)
{
  return &list->data[index_into(vm, list->len, idx)];
}

Value _vm_get_elem(Varmint *vm, Value collection, Value idx)
{
  switch (collection.type) {
  case V_string:
    {
      String *string = collection.as.string;
      char c = string->s[index_into(vm, string->len, idx)];
      return String_create(vm, &c, 1);
    }
  case V_list:
    return *index_list(vm, collection.as.list, idx);
  default:
    runtime_error(vm, "cannot index into %s\n",
        value_type_cstring(collection.type));
    unreachable();
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
  switch (collection.type) {
  case V_string:
    return set_string_idx(vm, collection.as.string, idx, val);
  case V_list:
    {
      Value *elem = index_list(vm, collection.as.list, idx);
      *elem = val;
      return val;
    }
  default:
    runtime_error(vm, "cannot index into %s\n",
        value_type_cstring(collection.type));
    unreachable();
  }
}

Value _typeof(Varmint *vm, Value *args)
{
  Value val = args[0];
  const char *type_string = value_type_cstring(val.type);

  return String_create(vm, type_string, strlen(type_string));
}

Value _lenof(Varmint *vm, Value *args)
{
  Value collection = args[0];

  switch (collection.type) {
  case V_range:
    {
      float64_t len = collection.as.range->end - collection.as.range->start;
      return value_new(len, number);
    }
  case V_string:
    return value_new((float64_t)collection.as.string->len, number);
  case V_list:
    return value_new((float64_t)collection.as.list->len, number);
  default:
    runtime_error(vm, "expect collection type, got %s",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

Value _put(Varmint *vm, Value *args)
{
  Value output_string = args[0];

  String *s = typechecked(vm, output_string, string);
  printf("%.*s", (int)s->len, s->s);

  return NO_VALUE;
}

Value _putln(Varmint *vm, Value *args)
{
  Value output_string = args[0];

  String *s = typechecked(vm, output_string, string);
  printf("%.*s\n", (int)s->len, s->s);

  return NO_VALUE;
}

Value _input(Varmint *vm, Value *_)
{
  return String_readline(vm, NULL);
}

Value _prompt(Varmint *vm, Value *args)
{
  Value prompt = args[0];
  return String_readline(vm, typechecked(vm, prompt, string)->s);
}

Value _to_number(Varmint *vm, Value *args)
{
  float64_t n;

  Value val = args[0];

  switch (val.type) {
  case V_no:
    unreachable();
  case V_number:
    n = val.as.number;
    break;
  case V_boolean:
    n = val.as.boolean ? 1 : 0;
    break;
  case V_string:
    {
      if (val.as.string->len == 0)
        goto no_num;

      char c = val.as.string->s[0];
      // man 3 strtod
      if (!isdigit(c)) switch (c) {
      case '+': case '-': // Optional sign
      case 'I': case 'i': // INFINITY
      case 'N': case 'n': // NAN
        break;
      default:
        // Invalid string!
        goto no_num;
      }

      char *endptr;
      n = strtod(val.as.string->s, &endptr);

      if (*endptr != '\0')
        // Invalid tailing characters!
        goto no_num;

      break;
    }
  case V_range:
  case V_native:
  case V_maybe:
  case V_list:
  case V_procedure:
    goto no_num;
  }

  return Maybe_some(vm, value_new(n, number));
no_num:
  return Maybe_none();
}

Value _rot(Varmint *vm, Value *args)
{
  Value shift = args[0],
        text = args[1];

  int shift_n = (int)typechecked(vm, shift, number);
  String *s = typechecked(vm, text, string);

  Value ciphertext = String_create(vm, s->s, s->len);

  // https://en.wikipedia.org/wiki/Caesar_cipher
  for (size_t i = 0; i < s->len; i++) {
    const char c = s->s[i];

    if (!isalpha(c))
      ciphertext.as.string->s[i] = c;

    else {
      char ciphered_c = (((toupper(c) - 'A') + shift_n) % 26) + 'A';
      if (islower(c))
        ciphered_c = (char)tolower(ciphered_c);

      ciphertext.as.string->s[i] = ciphered_c;
    }
  }

  return ciphertext;
}

