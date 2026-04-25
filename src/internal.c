#include "../inc/info.h"
#include "../inc/val.h"

#include <ctype.h>
#include <math.h>
#include <time.h>

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
    runtime_error(vm, "invalid operand types to concatenation");

  return String_concat(vm, &head, &tail);
}

Value _vm_in(Varmint *vm, Value x, Value collection)
{
  bool contains = false;

  switch (collection.type) {
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
  case V_list:
    for (size_t i = 0; i < collection.as.list->len; i++) {
      Value elem = collection.as.list->data[i];
      if (values_eq(x, elem)) {
        contains = true; break;
      }
    }
    break;
  case V_table:
    contains = Table_get(collection.as.table, x) != NULL;
    break;
  default:
    runtime_error(vm, "`in`: expect collection, got %s",
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
    runtime_error(vm, "index [%li] out of range (length %li)", _idx, len);

  return actual_idx;
}

static inline Value *index_list(Varmint *vm, List *list, Value idx)
{
  return &list->data[index_into(vm, list->len, idx)];
}

static inline Value *index_table(Varmint *vm, Table *table, Value key)
{
  if (!value_is_hashable(key.type))
    runtime_error(vm, "expect hashable key type, got %s",
        value_type_cstring(key.type));

  Value *result = Table_get(table, key);

  if (result == NULL) {
    String *s = value_to_string(vm, key).as.string;
    runtime_error(vm, "no value matching [%s] in table", (int)s->len, s->s);
  }

  return result;
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
  case V_table:
    return *index_table(vm, collection.as.table, idx);
  default:
    runtime_error(vm, "cannot index into %s",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

static Value set_string_idx(Varmint *vm, String *string, Value idx, Value val)
{
  if (val.type != V_string || val.as.string->len - 1 != 1)
    runtime_error(vm, "string index assignment must be a single character");

  string->s[index_into(vm, string->len, idx)] = val.as.string->s[0];
  return val;
}

Value _vm_set_elem(Varmint *vm, Value collection, Value idx, Value val)
{
  switch (collection.type) {
  case V_string:
    return set_string_idx(vm, collection.as.string, idx, val);
  case V_list:
    return *index_list(vm, collection.as.list, idx) = val;
  case V_table:
    return *index_table(vm, collection.as.table, idx) = val;
  default:
    runtime_error(vm, "cannot index into %s",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

Value _typeof(Varmint *vm, ArgList *args)
{
  Value val;
  if (!varm_arg(vm, args, "v", &val))
    return NO_VALUE;

  return String_from(vm, value_type_cstring(val.type));
}

Value _len(Varmint *vm, ArgList *args)
{
  Value collection;
  if (!varm_arg(vm, args, "v", &collection))
    return NO_VALUE;

  switch (collection.type) {
  case V_string:
    return value_new((float64_t)collection.as.string->len, number);
  case V_list:
    return value_new((float64_t)collection.as.list->len, number);
  case V_table:
    return value_new((float64_t)collection.as.table->entry_count, number);
  default:
    runtime_error(vm, "expect collection type, got %s",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

Value _to_number(Varmint *vm, ArgList *args)
{
  Value val;
  if (!varm_arg(vm, args, "v", &val))
    return NO_VALUE;

  float64_t n;

  switch (val.type) {
  case V_no: case V_upval:
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
  case V_native:
  case V_maybe:
  case V_list:
  case V_table:
  case V_procedure:
  case V_closure:
  case V_partial:
    goto no_num;
  }

  return Maybe_some(vm, value_new(n, number));
no_num:
  return Maybe_none();
}

Value _to_string(Varmint *vm, ArgList *args)
{
  Value val;
  if (!varm_arg(vm, args, "v", &val))
    return NO_VALUE;

  return value_to_string(vm, val);
}

Value _unwrap(Varmint *vm, ArgList *args)
{
  Maybe *unwrappee;
  if (!varm_arg(vm, args, "M", &unwrappee))
    return NO_VALUE;

  if (unwrappee == NULL) {
    runtime_error(vm, "unwrap of None");
    return NO_VALUE;
  }

  return unwrappee->raw;
}

Value _put(Varmint *vm, ArgList *args)
{
  String *output;
  if (!varm_arg(vm, args, "S", &output))
    return NO_VALUE;

  printf("%.*s", (int)output->len, output->s);
  return NO_VALUE;
}

Value _putln(Varmint *vm, ArgList *args)
{
  String *output;
  if (!varm_arg(vm, args, "S", &output))
    return NO_VALUE;

  printf("%.*s\n", (int)output->len, output->s);
  return NO_VALUE;
}

Value _input(Varmint *vm, ArgList *args)
{
  String *prompt;
  if (!varm_arg(vm, args, "S", &prompt))
    return NO_VALUE;

  return String_readline(vm, prompt->s);
}

Value _time(Varmint *vm, ArgList *args)
{
  if (!varm_arg(vm, args, ""))
    return NO_VALUE;

  clock_t time = clock();
  return value_new((float64_t)time, number);
}

Value _rot(Varmint *vm, ArgList *args)
{
  int shift;
  String *text;
  if (!varm_arg(vm, args, "iS", &shift, &text))
    return NO_VALUE;

  Value ciphertext = String_copy(vm, text);

  // https://en.wikipedia.org/wiki/Caesar_cipher
  for (size_t i = 0; i < text->len; i++) {
    const char c = text->s[i];

    if (!isalpha(c))
      ciphertext.as.string->s[i] = c;

    else {
      char ciphered_c = (((toupper(c) - 'A') + shift) % 26) + 'A';
      if (islower(c))
        ciphered_c = (char)tolower(ciphered_c);

      ciphertext.as.string->s[i] = ciphered_c;
    }
  }

  return ciphertext;
}

