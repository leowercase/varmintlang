#include "../inc/info.h"
#include "../inc/gc.h"
#include "../inc/val.h"
#include "../inc/code.h"

#include <stdio.h>
#include <readline/readline.h>
// https://github.com/Cyan4973/xxHash
#include <xxhash.h>

bool varm_arg(struct Varmint *vm,
    struct ArgList *args, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);

  size_t i;
  for (i = 0; format[i] != '\0'; i++) {
    Value arg = args->argv[i];
    Typetag argt = arg.type;

    switch (format[i]) {
      // Value
    case 'v':
      *va_arg(ap, Value *) = arg;
      break;
      // Number
    case 'n':
      *va_arg(ap, float64_t *) = typechecked(vm, arg, number);
      break;
      // Number cast to int
    case 'i':
      *va_arg(ap, int *) = (int)typechecked(vm, arg, number);
      break;
      // Boolean
    case 'b':
      *va_arg(ap, bool *) = typechecked(vm, arg, boolean);
      break;
      // Maybe
    case 'M':
      *va_arg(ap, Maybe **) = typechecked(vm, arg, maybe);
      break;
      // String
    case 'S':
      *va_arg(ap, String **) = typechecked(vm, arg, string);
      break;
      // List
    case 'L':
      *va_arg(ap, List **) = typechecked(vm, arg, list);
      break;
      // Table
    case 'T':
      *va_arg(ap, Table **) = typechecked(vm, arg, table);
      break;
      // Callable
    case 'C':
      if (!value_is_callable(argt))
        runtime_error(vm, "expect callable arg, got %s",
            value_type_cstring(argt));
      *va_arg(ap, Value *) = arg;
      break;
    default:
      unreachable();
    }
  }

  if (i != args->argc)
    runtime_error(vm, "expect %li args to function, got %li",
        i, args->argc);

  va_end(ap);
  return vm->status == VM_A_OK;
}

Value Maybe_some(Varmint *vm, Value raw)
{
  Maybe *mbe = (Maybe *)create_gc_obj(vm, V_maybe, sizeof(Maybe));
  mbe->raw = raw;
  return value_new(mbe, maybe);
}

Value Maybe_none(void)
{
  // None isn't GC'd.
  Value val = {V_maybe, .as.maybe = NULL};
  return val;
}

Value List_create(Varmint *vm, size_t cap)
{
  List *l = (List *)create_gc_obj(vm, V_list, sizeof(List));

  l->len = l->cap = 0;
  l->data = NULL;

  List_adjust_cap(vm, l, cap);
  return value_new(l, list);
}

Value Table_create(Varmint *vm, size_t entry_count)
{
  Table *tb = (Table *)create_gc_obj(vm, V_table, sizeof(Table));

  tb->entry_count = tb->cap = 0;
  tb->entries = NULL;

  if (entry_count > 0) Table_reserve_size(vm, tb, entry_count);
  return value_new(tb, table);
}

static Value String_bare(Varmint *vm, char *s, size_t len)
{
  String *t = (String *)create_gc_obj(vm, V_string, sizeof(String));
  t->s = s;
  t->len = len;
  return value_new(t, string);
}

Value String_create(Varmint *vm, const char *s, size_t len)
{
  char *allocd_chars = gc_alloc(vm, NULL, 0, len * sizeof(char) + sizeof('\0'));

  memcpy(allocd_chars, s, len);
  allocd_chars[len] = '\0';

  return String_bare(vm, allocd_chars, len);
}

Value String_from(Varmint *vm, const char *s)
{
  return String_create(vm, s, strlen(s));
}

Value String_own(Varmint *vm, char *allocated_cstring)
{
  size_t len = strlen(allocated_cstring);
  gc_own_bytes(vm, len * sizeof(char));

  return String_bare(vm, allocated_cstring, len);
}

Value String_copy(Varmint *vm, String *string)
{
  return String_create(vm, string->s, string->len);
}

// Format strings just like sprintf et al., except retaining sanity
Value String_fmt(Varmint *vm, const char *fmt, ...)
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
    runtime_error(vm, "string formatting failed");

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
    runtime_error(vm, "string formatting failed");
  }

  return String_bare(vm, s, len);
}

Value String_concat(Varmint *vm, Value *head, Value *tail)
{
  String *head_s = head->as.string, *tail_s = tail->as.string;
  size_t len = head_s->len + tail_s->len;
  char *s = gc_alloc(vm, NULL, 0, len * sizeof(char) + sizeof('\0'));

  memcpy(s, head_s->s, head_s->len);
  memcpy(s + head_s->len, tail_s->s, tail_s->len + 1);

  return String_bare(vm, s, len);
}

Value String_readline(Varmint *vm, const char *prompt)
{
  char *line = readline(prompt);

  if (line == NULL)
    return String_bare(vm, NULL, 0);
  else
    return String_own(vm, line);
}

Str String_as_str(Value *val)
{
  return str_new(val->as.string->s, val->as.string->len);
}

Value Procedure_create(Varmint *vm, size_t arity, String *source)
{
  Procedure *proc = (Procedure *)
    create_gc_obj(vm, V_procedure, sizeof(Procedure));

  // Initialize p-code
  proc->code.constants = Constants_init();
  proc->code.instructions = Instructions_init();
  proc->code.lines = LineInfo_init();

  // Keep track of source code.
  proc->source = source;

  // Initialize closure description
  proc->closure_desc = ClosureDesc_init();

  proc->arity = arity;
  proc->name = NULL_STR;
  return value_new(proc, procedure);
}

// Allocate a closure and its upvalues.
Value Closure_create(Varmint *vm, Procedure *procedure)
{
  ClosureDesc desc = procedure->closure_desc;

  size_t size = sizeof(Closure) + desc.len * sizeof(Upval *);
  Closure *c = (Closure *)create_gc_obj(vm, V_closure, size);

  c->upvalue_count = desc.len;
  c->procedure = procedure;

  return value_new(c, closure);
}

// Allocate a partial application.
Value Partial_create(Varmint *vm, Value callee, size_t count)
{
  size_t size = sizeof(Partial) + count * sizeof(Value);
  Partial *p = (Partial *)create_gc_obj(vm, V_partial, size);

  p->callee = callee;
  p->application_count = count;
  return value_new(p, partial);
}

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
    case V_no: case V_upval:
      unreachable();
    case V_number:
      return a.as.number == b.as.number;
    case V_boolean:
      return a.as.boolean == b.as.boolean;
    case V_native:
      return a.as.native == b.as.native;
    case V_maybe:
      {
        bool a_none = a.as.maybe == NULL,
             b_none = b.as.maybe == NULL;
        return (a_none && b_none)
          || (!a_none && !b_none &&
              values_eq(a.as.maybe->raw, b.as.maybe->raw));
      }
    case V_string:
      return strs_eq(String_as_str(&a), String_as_str(&b));
    case V_list:
    case V_table:
    case V_procedure:
    case V_closure:
    case V_partial:
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
  case_(table)
  case_(procedure)
  case_(upval)
  case_(closure)
  case_(partial)
  }

#undef case_
}

static Value fn_to_string(Varmint *vm, const char *moniker, Str name)
{
  if (name.s != NULL)
    return String_fmt(vm, "<%s %.*s>", moniker, (int)name.len, name.s);
  else
    return String_fmt(vm, "<%s>", moniker);
}

Value value_to_string(Varmint *vm, Value val)
{
  switch (val.type) {
  case V_no: case V_upval:
    unreachable();
  case V_number:
    return String_fmt(vm, "%g", val.as.number);
  case V_boolean:
    return val.as.boolean ? String_from(vm, "True") : String_from(vm, "False");
  case V_native:
    return String_from(vm, "native fn");
  case V_maybe:
    if (val.as.maybe == NULL)
      return String_from(vm, "");
    else
      return value_to_string(vm, val.as.maybe->raw);
  case V_string:
    return String_copy(vm, val.as.string);
  case V_list:
    return String_from(vm, "<list>");
  case V_table:
    return String_from(vm, "<table>");
  case V_procedure:
    return fn_to_string(vm, "fn", val.as.procedure->name);
  case V_closure:
    return fn_to_string(vm, "closure", val.as.closure->procedure->name);
  case V_partial:
    return String_from(vm, "<partial>");
  }
}

static void print_fn(FILE *restrict stream, const char *moniker, Str name)
{
  fprintf(stream, ANSI_GREEN);
  if (name.s != NULL)
    fprintf(stream, "<%s %.*s>", moniker, (int)name.len, name.s);
  else
    fprintf(stream, "<%s>", moniker);
  fprintf(stream, ANSI_RESET);
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
    if (val.as.maybe == NULL)
      fprintf(stream, ANSI_BLUE "None" ANSI_RESET);
    else {
      fprintf(stream, ANSI_BLUE "Some" ANSI_RESET "(");
      print_value(stream, val.as.maybe->raw);
      fprintf(stream, ")");
    }
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

      fprintf(stream, ANSI_MAGENTA "]" ANSI_RESET "(%li)",
          list->len);
      break;
    }
  case V_table:
    {
      Table *table = val.as.table;
      fprintf(stream, ANSI_MAGENTA "@[" ANSI_RESET);

      for (size_t i = 0, ents = 0; i < table->cap; i++) {
        TableEntry ent = table->entries[i];

        if (ent.is_tomb || ent.key.type == V_no) continue;
        ents++;

        if (ent.key.type == V_string) {
          String *key = ent.key.as.string;
          fprintf(stream, "%.*s", (int)key->len, key->s);
        }
        else {
          fprintf(stream, "[");
          print_value(stream, ent.key);
          fprintf(stream, "]");
        }

        fprintf(stream, " := ");
        print_value(stream, ent.value);

        if (ents < table->entry_count)
          fprintf(stream, ", ");
      }

      fprintf(stream, ANSI_MAGENTA "]" ANSI_RESET "(%li)",
          table->entry_count);
      break;
    }
  case V_procedure:
    print_fn(stream, "fn", val.as.procedure->name);
    break;
  case V_upval:
    unreachable();
  case V_closure:
    print_fn(stream, "closure", val.as.closure->procedure->name);
    break;
  case V_partial:
    fprintf(stream, "partial [%li] ", val.as.partial->application_count);
    print_value(stream, val.as.partial->callee);
    break;
  }
}

uint64_t hash_value(Value val)
{
  switch (val.type) {
  case V_number:
    return XXH3_64bits(&val.as.number, sizeof(float64_t));
  case V_boolean:
    return XXH3_64bits(&val.as.boolean, sizeof(int));
  case V_native:
    return XXH3_64bits(&val.as.native, sizeof(NativeFn));
  case V_maybe:
    return val.as.maybe != NULL ? hash_value(val.as.maybe->raw) : 0;
  case V_string:
    return XXH3_64bits(val.as.string->s, val.as.string->len);
  default:
    unreachable(); // Call `value_is_hashable` first!
  }
}

bool value_is_hashable(Typetag t)
{
  switch (t) {
  case V_no:
  case V_upval:
    unreachable();
  case V_number:
  case V_boolean:
  case V_native:
  case V_maybe:
  case V_string:
    return true;
  default:
    return false;
  }
}

bool value_is_callable(Typetag t)
{
  switch (t) {
  case V_native:
  case V_procedure:
  case V_closure:
  case V_partial:
    return true;
  default:
    return false;
  }
}
