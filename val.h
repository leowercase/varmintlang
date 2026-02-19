#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "generic/dyn_array.h"
#include "str.h"
#include "util.h"

#include <assert.h>

/*
 * The language has values of type number, boolean, string, list and function.
 * It is dynamically typed; values are checked during runtime, not compiletime
 */

typedef enum {
  // Internal value type, null equivalent.
  // No runtime values are allowed to have the this type though.
  VAL_no,

  // Simple values
  VAL_number,
  VAL_boolean,

  VAL_native,

  // GC'd values
  VAL_string,
  VAL_list,
  VAL_function,

  // Internal values.
  VAL_program,
} ValueType;

typedef union {
  float64_t number;
  int boolean;

  struct NativeFn *native;

  struct GCData *gc_data; // Accessed by the garbage collector.

  struct StringValue *string;
  struct ValueList *list;
  struct Proc *function;

  struct Proc *program;
} RawValue;

typedef struct {
  ValueType type;
  RawValue raw;
} Value;

static inline
Value __value_new(ValueType type, RawValue raw)
{
  Value val = {type, raw};
  return val;
}
#define value_new(raw, vat_t) \
  __value_new(VAL_##vat_t, (RawValue){.vat_t = raw})

// GC'd values have the same initial sequence, GCData.
typedef struct GCData {
  Value *next;
  bool marked;
} GCData;

static inline
Value __heaped_value_new(ValueType type, size_t size)
{
  RawValue raw;
  raw.gc_data = malloc(size);
  if (raw.gc_data == NULL)
    exit(EX_OSERR);

  Value val = {type, raw};
  return val;
}
#define heaped_value_new(type, vat_t) \
  __heaped_value_new(VAL_##vat_t, sizeof(type))

static inline
bool is_heaped_value(ValueType type)
{
  return type >= VAL_string;
}

static const RawValue EMPTY_RAW_VAL = {0};
static const Value NO_VAL = {VAL_no, EMPTY_RAW_VAL};

typedef struct StringValue {
  GCData gc_data;
  Str str;
} StringValue;

static inline
Value string_value_new(Str str)
{
  Value val = heaped_value_new(StringValue, string);
  val.raw.string->str = str;
  return val;
}

typedef struct ValueList {
  GCData gc_data;
  DYN_ARRAY(Value)
} ValueList;
#define T Value
#define ARR ValueList
#include "generic/dyn_array.inc"

bool values_eq(Value a, Value b);
bool value_is_falsey(Value val);

char *value_type_cstring(ValueType type);
Str value_to_str(Value val);
void print_value(Value val);

#endif
