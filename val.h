#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "generic/dyn_array.h"
#include "generic/table.h"
#include "str.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>

// Dynamic typing; values carry a typetag during runtime.
typedef enum {
  // Internal value type, null equivalent.
  // No runtime values are allowed to have the this type though.
  V_no,

  // Simple values
  V_number,
  V_boolean,
  V_native,

  // GC'd values
  V_maybe,
  V_range,
  V_string,
  V_list,
  V_table,
  V_procedure,
  V_upval,
  V_closure,
} Typetag;

// Union of Varmint values.
typedef union {
  float64_t number;
  int boolean;
  size_t native;

  struct GCData *gc_data; // Accessed by the garbage collector.
  struct Maybe *maybe;
  struct Range *range;
  struct String *string;
  struct List *list;
  struct Table *table;
  struct Procedure *procedure;
  struct Upval *upval;
  struct Closure *closure;
} Valueu;

typedef struct {
  Typetag type;
  Valueu as;
} Value;

static inline
Value __value_new(Valueu raw, Typetag tag)
{
  Value val = {tag, raw};
  return val;
}
#define value_new(raw, t) \
  __value_new((Valueu){.t = raw}, V_##t)

static const Value NO_VALUE = {V_no, {0}};

bool values_eq(Value a, Value b);
bool value_is_falsey(Value val);

// Obtain the 64-bit hash of a value.
uint64_t hash_value(Value val);
bool value_is_hashable(Typetag t);

// GC'd values have the same initial sequence, GCData.
typedef struct GCData {
  Value *next;
  bool is_safe;
} GCData;

static inline
bool is_heaped_value(Value val)
{
  return val.type == V_maybe
    ? val.as.maybe != NULL : val.type > V_maybe;
}

// Optional or nullable type.
// None is represented as (Maybe *)NULL
typedef struct Maybe {
  GCData gc_data;
  Value raw;
} Maybe;

// A real interval.
// Why half open by default?
// - https://www.cs.utexas.edu/~EWD/ewd08xx/EWD831.PDF
typedef struct Range {
  GCData gc_data;
  float64_t start, end;
  bool inclusive; // [start, end) or [start, end]
} Range;

typedef struct String {
  GCData gc_data;
  char *s;
  size_t len;
} String;

typedef struct List {
  GCData gc_data;
  DYN_ARRAY(Value)
} List;
#define T Value
#define ARR List
#define USE_GC
#include "generic/dyn_array.inc"

typedef TABLE_ENTRY_STRUCT(Value, Value) TableEntry;
typedef struct Table {
  GCData gc_data;
  TABLE(TableEntry)
} Table;
#define K Value
#define V Value
#define TBL_ENTRY TableEntry
#define TBL Table
#define HASH(key) hash_value(key)
#define EMPTY_KEY NO_VALUE
#define IS_EMPTY_KEY(key) (key.type == V_no)
#define KEYS_EQ(a, b) values_eq(a, b)
#define USE_GC
#include "generic/table.inc"

struct Varmint;

Value Maybe_some(struct Varmint *vm, Value value);
Value Maybe_none(void);

Value List_create(struct Varmint *vm, size_t cap);

Value Table_create(struct Varmint *vm, size_t cap);

Value Range_create(struct Varmint *vm, float64_t start, float64_t end,
                                       bool end_inclusive);

Value String_create(struct Varmint *vm, const char *s, size_t len);
Value String_from(struct Varmint *vm, const char *cstring);
Value String_own(struct Varmint *vm, char *allocated_cstring);
Value String_copy(struct Varmint *vm, Value *string_val);
Value String_fmt(struct Varmint *vm, const char *fmt, ...);
Value String_concat(struct Varmint *vm, Value *head, Value *tail);
Value String_readline(struct Varmint *vm, const char *prompt);
Str String_as_str(Value *val);

Value Procedure_create(struct Varmint *vm, size_t arity);

Value Closure_create(struct Varmint *vm, struct Procedure *procedure);

const char *value_type_cstring(Typetag type);
Value value_to_string(struct Varmint *vm, Value val);
void print_value(FILE *restrict stream, Value val);

#endif
