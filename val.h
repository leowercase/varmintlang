#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "str.h"
#include "util.h"

#include <assert.h>

/*
 * The language has values of type number, boolean, string and list.
 * It is dynamically typed; values are checked during runtime, not compiletime
 */

typedef enum {
  // Internal value type, null equivalent.
  // No runtime values are allowed to have the this type though.
  VAL_no,

  // Simple values
  VAL_number,
  VAL_boolean,

  // GC'd values
  VAL_string,
  VAL_list,
} ValueType;

typedef union {
  float64_t number;
  int boolean;

  struct GCData *gc_data; // Accessed by the garbage collector.

  struct StringVal *string;
  struct ListVal *list;
} RawValue;

typedef struct {
  ValueType type;
  RawValue raw;
} Value;

// GC'd values have the same initial sequence, GCData.
typedef struct GCData {
  Value *next;
  bool marked;
} GCData;

static inline
Value __value_new(ValueType type, RawValue raw)
{
  Value val = {type, raw};
  return val;
}
#define value_new(raw, vat_t) __value_new(VAL_##vat_t, (RawValue)(raw))

#define is_type(val, vat_t) ((val).type == VAL_##vat_t)

// Helper macro.
#define typechecked(val_ident, vat_t, ...) \
  (is_type(val_ident, vat_t) ? \
    val_ident.raw.vat_t : \
    (runtime_error("Expect type " #vat_t " for " #val_ident ", got %s\n", \
                   value_type_cstring(val_ident.type)), EMPTY_RAW_VAL.vat_t))

static const RawValue EMPTY_RAW_VAL = {0};
static const Value NO_VAL = {VAL_no, EMPTY_RAW_VAL};

typedef struct StringVal {
  GCData gc_data;
  Str str;
} StringVal;

static inline
StringVal *stringval_new(Str str)
{
  StringVal *val = (StringVal *)malloc(sizeof(StringVal));
  if (val == NULL)
    exit(EX_OSERR);
  val->str = str;
  return val;
}

#define T Value
#define TYPE_NAME List
#include "dyn_array.h"

typedef struct ListVal {
  GCData gc_data;
  List list;
} ListVal;

bool values_eq(Value a, Value b);
bool is_falsey(Value val);

char *value_type_cstring(ValueType type);
Str value_to_str(Value val);
void print_value(Value val);

#endif
