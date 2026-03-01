#include "error.h"
#include "gc.h"
#include "val.h"
#include "proc.h"

#include <stdio.h>
// https://github.com/Cyan4973/xxHash
#include <xxhash.h>

Value *Maybe_some(Varmint *vm, Value raw)
{
  Value *val = create_gc_obj(vm, V_maybe, sizeof(Maybe));
  val->as.maybe->is_some = true;
  val->as.maybe->raw = raw;
  return val;
}

Value *Maybe_none(Varmint *vm)
{
  // Don't even allocate space for the empty raw field; it is extraneous
  const size_t none_size = sizeof(Maybe) - sizeof(Value);

  Value *val = create_gc_obj(vm, V_maybe, none_size);
  val->as.maybe->is_some = false;
  return val;
}

Value *List_create(Varmint *vm, size_t cap)
{
  Value *val = create_gc_obj(vm, V_list, sizeof(List));

  List *l = val->as.list;
  l->len = l->cap = 0;
  l->data = NULL;

  List_adjust_cap(vm, l, cap);
  return val;
}

Value *String_create(Varmint *vm, const char *s, size_t len)
{
  Value *val = create_gc_obj(vm, V_string, sizeof(String));
  val->as.string->len = len;

  val->as.string->s = gc_alloc(vm, NULL, 0, len * sizeof(char) + sizeof('\0'));
  memcpy(val->as.string->s, s, len);
  val->as.string->s[len] = '\0';

  return val;
}

Value *String_from(Varmint *vm, const char *s)
{
  return String_create(vm, s, strlen(s));
}

Value *String_own(Varmint *vm, char *allocated_cstring)
{
  Value *val = create_gc_obj(vm, V_string, sizeof(String));

  size_t len = strlen(allocated_cstring);
  val->as.string->len = len;

  val->as.string->s = allocated_cstring;
  gc_own_bytes(vm, len * sizeof(char));

  return val;
}

Value *String_copy(Varmint *vm, Value *string_val)
{
  return String_create(vm,
      string_val->as.string->s, string_val->as.string->len);
}

// Format strings just like sprintf et al., except retaining sanity
Value *String_fmt(Varmint *vm, const char *fmt, ...)
{
  // man 3 vnsprintf
  va_list a;
  int n;

  // Determine required size for format string.
  va_start(a, fmt);
  n = vsnprintf(NULL, 0, fmt, a);
  va_end(a);

  if (n < 0)
    // Error.
    runtime_error(vm, "string formatting failed\n");

  size_t len = (size_t)n,
         size_bytes = len * sizeof(char) + sizeof('\0');
  char *s = gc_alloc(vm, NULL, 0, size_bytes);

  // Actually do the thing™
  va_start(a, fmt);
  n = vsnprintf(s, len + 1, fmt, a);
  va_end(a);

  if (n < 0) {
    // Again, error.
    gc_free(vm, s, size_bytes);
    runtime_error(vm, "string formatting failed\n");
  }

  Value *string = create_gc_obj(vm, V_string, sizeof(String));
  string->as.string->len = len;
  string->as.string->s = s;

  return string;
}

Value *String_concat(Varmint *vm, Value *head, Value *tail)
{
  String *head_s = head->as.string, *tail_s = tail->as.string;
  size_t len = head_s->len + tail_s->len;
  char *s = gc_alloc(vm, NULL, 0, len * sizeof(char) + sizeof('\0'));

  memcpy(s, head_s->s, head_s->len);
  memcpy(s + head_s->len, tail_s->s, tail_s->len + 1);

  Value *result = create_gc_obj(vm, V_string, sizeof(String));
  result->as.string->s = s;
  result->as.string->len = len;
  return result;
}

Str String_as_str(Value *val)
{
  return str_new(val->as.string->s, val->as.string->len);
}

Value *Procedure_create(Varmint *vm, size_t arity)
{
  Value *val = create_gc_obj(vm, V_procedure, sizeof(Procedure));
  val->as.procedure->arity = arity;
  val->as.procedure->code = new_p_code(); // Code allocation isn't GC'd.
  return val;
}

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
    case V_no:
      unreachable();
    case V_number:
      return a.as.number == b.as.number;
    case V_boolean:
      return a.as.boolean == b.as.boolean;
    case V_native:
      return a.as.native == b.as.native;
    case V_maybe:
      return a.as.maybe->is_some == b.as.maybe->is_some
        && (!a.as.maybe->is_some
            || values_eq(a.as.maybe->raw, b.as.maybe->raw));
    case V_string:
      return strs_eq(String_as_str(&a), String_as_str(&b));
    case V_list:
    case V_procedure:
      // "Shallow" equivalence
      return a.as.gc_data == b.as.gc_data;
    }
  }
}

bool value_is_falsey(Value val)
{
  if (val.type == V_boolean)
    return !val.as.boolean;
  else
    return false;
}

const char *value_type_cstring(Typetag type)
{
#define case_(name) case V_##name: return #name;

  switch (type) {
  case V_no:
    // Only has internal usage in error messages
    return "no value";
  case_(number)
  case_(boolean)
  case_(native)
  case_(maybe)
  case_(string)
  case_(list)
  case_(procedure)
  }

#undef case_
}

static Value *fn_to_string(Varmint *vm, const char *moniker, Str name)
{
  if (name.s != NULL)
    return String_fmt(vm, "<%s %.*s>", moniker, (int)name.len, name.s);
  else
    return String_fmt(vm, "<%s>", moniker);
}

Value *value_to_string(Varmint *vm, Value val)
{
  switch (val.type) {
  case V_no:
    unreachable();
  case V_number:
    return String_fmt(vm, "%g", val.as.number);
  case V_boolean:
    return val.as.boolean ? String_from(vm, "True") : String_from(vm, "False");
  case V_maybe:
    if (val.as.maybe->is_some) {
      String *some = value_to_string(vm, val.as.maybe->raw)->as.string;
      return
        String_fmt(vm, "Some(%.*s)", (int)some->len, some->s);
    }
    else
      return String_from(vm, "None");
  case V_string:
    return String_copy(vm, &val);
  case V_list:
    return String_from(vm, "<list>");
  case V_procedure:
    return fn_to_string(vm, "fn", val.as.procedure->name);
  case V_native:
    return fn_to_string(vm, "native fn", vm->natives.data[val.as.native].name);
  }
}

void print_value(FILE *restrict stream, Value val)
{
  switch (val.type) {
  case V_no:
    fprintf(stream, ANSI_WHITE "no value" ANSI_RESET);
    break;
  case V_number:
    fprintf(stream, ANSI_RED "%g" ANSI_RESET, val.as.number);
    break;
  case V_boolean:
    fprintf(stream, ANSI_BLUE "%s" ANSI_RESET,
        val.as.boolean ? "True" : "False");
    break;
  case V_native:
    fprintf(stream, ANSI_GREEN "<native fn>" ANSI_RESET); break;
  case V_maybe:
    if (val.as.maybe->is_some) {
      fprintf(stream, ANSI_BLUE "Some" ANSI_RESET "(");
      print_value(stream, val.as.maybe->raw);
      fprintf(stream, ")");
    }
    else
      fprintf(stream, ANSI_BLUE "None" ANSI_RESET);
    break;
  case V_string:
    fprintf(stream, ANSI_YELLOW "\"%s\"" ANSI_RESET "(%li)",
        val.as.string->s, val.as.string->len);
    break;
  case V_list:
    {
      List *list = val.as.list;
      fprintf(stream, ANSI_MAGENTA "[");
      for (size_t i = 0; i < list->len; i++) {
        print_value(stream, list->data[i]);
        if (i < list->len - 1)
          fprintf(stream, ", ");
      }
      fprintf(stream, ANSI_MAGENTA "]" ANSI_RESET);
      break;
    }
  case V_procedure:
    {
      Str name = val.as.procedure->name;
      fprintf(stream, ANSI_GREEN);
      if (name.s != NULL)
        fprintf(stream, "<fn %.*s>", (int)name.len, name.s);
      else
        fprintf(stream, "<fn>");
      fprintf(stream, ANSI_RESET);
      break;
    }
  }
}

uint64_t hash_value(Value val)
{
  switch (val.type) {
  case V_no:
    unreachable();
  case V_number:
    return XXH3_64bits(&val.as.number, sizeof(float64_t));
  case V_boolean:
    return XXH3_64bits(&val.as.boolean, sizeof(int));
  case V_native:
    return XXH3_64bits(&val.as.native, sizeof(size_t));
  case V_maybe:
    return val.as.maybe->is_some ? hash_value(val.as.maybe->raw) : 0;
  case V_string:
    return XXH3_64bits(val.as.string->s, val.as.string->len);
  case V_list:
  case V_procedure:
    return XXH3_64bits(&val.as.gc_data, sizeof(GCData *));
  }
}
