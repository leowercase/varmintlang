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
  int _n = (int)typechecked(vm, n, number);

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

Value _vm_ncat(Varmint *vm, Value cattee, Value n)
{
  size_t _n = (size_t)typechecked(vm, n, number);

  switch (cattee.type) {
  case V_string:
    {
      String *string = cattee.as.string;

      size_t len = string->len * _n;

      char *s = gc_alloc(vm, NULL, 0, len * sizeof(char) + sizeof('\0'));

      for (size_t i = 0; i < _n; i++) {
        size_t size = string->len * sizeof(char);
        memcpy(&s[i * string->len], string->s, size);
      }
      s[len] = '\0';

      return String_bare(vm, s, len);
    }

  case V_list:
    {
      List *list = cattee.as.list;

      size_t len = list->len * _n;

      Value result = List_create(vm, len);
      result.as.list->len = len;

      for (size_t i = 0; i < _n; i++) {
        size_t size = list->len * sizeof(Value);
        memcpy(&result.as.list->data[i * list->len], list->data, size);
      }

      return result;
    }

  default:
    runtime_error(vm,
        "expect type string or list for ncatenation, got %s",
        typetag_cstring(cattee.type));
    return NO_VALUE;
  }
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
        typetag_cstring(collection.type));
  }

  return value_new(contains, boolean);
}

Value _vm_notin(Varmint *vm, Value x, Value collection)
{
  return value_new(
      value_is_falsey(_vm_in(vm, x, collection)), boolean);
}

typedef struct {
  size_t idx;
  bool success;
} IndexResult;

static IndexResult index_into(Varmint *vm, size_t len, Value idx)
{
  float64_t i = typechecked(vm, idx, number);
  size_t actual_idx;

  if (i >= 0)
    // Index normally.
    actual_idx = (size_t)i;

  else {
    // Index from the top with a negative number.
    size_t abs_i = (size_t)-i;
    if (abs_i > len) goto out_of_range;
    actual_idx = len - abs_i;
  }

  if (actual_idx >= len) goto out_of_range;

  return (IndexResult){actual_idx, .success = true};

out_of_range:
  // Index is out of range for the collection.
  return (IndexResult){0, .success = false};
}

static inline void index_out_of_range(Varmint *vm, size_t len, Value idx)
{
  runtime_error(vm, "index %g out of range (length %li)",
      idx.as.number, len);
}

static bool valid_table_key(Varmint *vm, Value key)
{
  if (!value_is_hashable(key.type)) {
    runtime_error(vm, "expect hashable key type, got %s",
        typetag_cstring(key.type));

    return false;
  }
  else return true;
}

Value _vm_get_elem(Varmint *vm, Value collection, Value idx, bool wrap_maybe)
{
  Value elem;
  bool success = false;

  switch (collection.type) {
  case V_string:
    {
      String *string = collection.as.string;
      IndexResult result = index_into(vm, string->len, idx);

      if (result.success) {
        elem = String_create(vm, &string->s[result.idx], 1);
        success = true;
      }
      else if (!wrap_maybe)
        index_out_of_range(vm, string->len, idx);
      break;
    }
  case V_list:
    {
      List *list = collection.as.list;
      IndexResult result = index_into(vm, list->len, idx);

      if (result.success) {
        elem = list->data[result.idx];
        success = true;
      }
      else if (!wrap_maybe)
        index_out_of_range(vm, list->len, idx);
      break;
    }
  case V_table:
    {
      if (!valid_table_key(vm, idx)) break;

      Value *result = Table_get(collection.as.table, idx);

      if (result != NULL) {
        elem = *result;
        success = true;
      }
      else if (!wrap_maybe) {
        String *s = value_to_string(vm, idx).as.string;
        runtime_error(vm,
            "no value for key %.*s in table", (int)s->len, s->s);
      }
      break;
    }
  default:
    runtime_error(vm, "cannot index into %s",
        typetag_cstring(collection.type));
  }

  if (wrap_maybe)
    return success ? Maybe_some(vm, elem) : Maybe_none();
  else
    return success ? elem : NO_VALUE;
}

Value _vm_set_elem(Varmint *vm, Value collection, Value idx, Value val)
{
  switch (collection.type) {
  case V_string:
    {
      String *string = collection.as.string;

      if (val.type != V_string || val.as.string->len != 1) {
        runtime_error(vm,
            "string index assignment must be a single character");
        return NO_VALUE;
      }

      IndexResult result = index_into(vm, string->len, idx);

      if (result.success) {
        string->s[result.idx] = val.as.string->s[0];
        return val;
      }
      else
        return NO_VALUE;
    }
  case V_list:
    {
      List *list = collection.as.list;
      IndexResult result = index_into(vm, list->len, idx);

      if (result.success)
        return list->data[result.idx] = val;
      else
        return NO_VALUE;
    }
  case V_table:
    {
      if (!valid_table_key(vm, idx))
        return NO_VALUE;

      Table_set(vm, collection.as.table, idx, val);
      return val;
    }
  default:
    runtime_error(vm, "cannot index into %s",
        typetag_cstring(collection.type));
    return NO_VALUE;
  }
}

Value _typeof(Varmint *vm, ArgList *args)
{
  Value val;
  if (!varm_arg(vm, args, "v", &val))
    return NO_VALUE;

  return String_from(vm, typetag_cstring(val.type));
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
        typetag_cstring(collection.type));
    return NO_VALUE;
  }
}

Value _push(Varmint *vm, ArgList *args)
{
  List *list;
  Value val;
  if (!varm_arg(vm, args, "Lv", &list, &val))
    return NO_VALUE;

  List_push(vm, list, val);
  return args->argv[0]; // List
}

Value _pop(Varmint *vm, ArgList *args)
{
  List *list;
  if (!varm_arg(vm, args, "L", &list))
    return NO_VALUE;

  return List_pop(list);
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
        // Invalid trailing characters!
        goto no_num;

      break;
    }
  case V_native:
  case V_maybe:
  case V_list:
  case V_table:
  case V_procedure:
  case V_closure:
  case V_cclosure:
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

static Value range_iter(Varmint *vm, ArgList *args, Value *upvalues)
{
  if (!varm_arg(vm, args, ""))
    return NO_VALUE;

  float64_t *iter = &upvalues[0].as.number;

  float64_t end = upvalues[1].as.number,
            step = upvalues[2].as.number;

  float64_t i = *iter;

  if (step >= 0) {
    // Positive step
    if (i >= end) return Maybe_none();
  }
  else {
    // Negative step
    if (i <= end) return Maybe_none();
  }

  (*iter) += step;
  return Maybe_some(vm, value_new(i, number));
}

Value _range(Varmint *vm, ArgList *args)
{
  float64_t start, end, step;

  if (!varm_arg(vm, args, "nnn", &start, &end, &step))
    return NO_VALUE;

  Value upvalues[] = {
    value_new(start, number),
    value_new(end, number),
    value_new(step, number),
  };

  return Cclosure_create(vm, range_iter, 3, upvalues);
}

Value string_items_iter(Varmint *vm, ArgList *args, Value *upvalues)
{
  if (!varm_arg(vm, args, ""))
    return NO_VALUE;

  float64_t *idx = &upvalues[0].as.number;
  String *s = upvalues[1].as.string;

  size_t i = (size_t)*idx;
  if (i >= s->len)
    return Maybe_none();

  Value result = String_create(vm, &s->s[i], 1);
  (*idx)++;
  return Maybe_some(vm, result);
}

Value list_items_iter(Varmint *vm, ArgList *args, Value *upvalues)
{
  if (!varm_arg(vm, args, ""))
    return NO_VALUE;

  float64_t *idx = &upvalues[0].as.number;
  List *list = upvalues[1].as.list;

  size_t i = (size_t)*idx;
  if (i >= list->len)
    return Maybe_none();

  Value result = list->data[i];
  (*idx)++;
  return Maybe_some(vm, result);
}

Value table_items_iter(Varmint *vm, ArgList *args, Value *upvalues)
{
  if (!varm_arg(vm, args, ""))
    return NO_VALUE;

  float64_t *idx = &upvalues[0].as.number;
  Table *table = upvalues[1].as.table;

  size_t i = (size_t)*idx;

  for (; i < table->cap; i++) {
    bool is_empty_entry =
      table->entries[i].is_tomb || table->entries[i].key.type == V_no;

    if (!is_empty_entry) break;
  }

  (*idx) = (float64_t)i;

  if (i >= table->cap)
    return Maybe_none();
  else
    return Maybe_some(vm, table->entries[i].key);
}

Value _items(Varmint *vm, ArgList *args)
{
  Value val;
  if (!varm_arg(vm, args, "v", &val))
    return NO_VALUE;

  Value upvalues[] = {
    value_new(0, number),
    val,
  };
  CclosureFn fn;

  switch (val.type) {
  case V_string:
    fn = string_items_iter; break;
  case V_list:
    fn = list_items_iter; break;
  case V_table:
    fn = table_items_iter; break;
  default:
    runtime_error(vm, "expect collection type for arg");
    return NO_VALUE;
  }

  return Cclosure_create(vm, fn, 2, upvalues);
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
