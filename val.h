#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "util.h"
#include "str.h"

#include <assert.h>

typedef enum {
  VAL_no,
  VAL_number,
  VAL_boolean,
  VAL_string,
} ValueType;

typedef union {
  float64_t number;
  int boolean;
  Str string;
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
#define value_new(raw, vat_t) __value_new(VAL_##vat_t, (RawValue)(raw))

#define is_type(val, vat_t) ((val).type == VAL_##vat_t)

// Allows some "cheating" with the type system et al.
static const RawValue EMPTY_RAW_VAL = {0};
static const Value NO_VAL = {VAL_no, EMPTY_RAW_VAL};

// Helper macro.
#define typechecked(val_ident, vat_t, ...) \
  (is_type(val_ident, vat_t) ? \
    val_ident.raw.vat_t : \
    (runtime_error("Expect type " #vat_t " for " #val_ident ", got %s\n", \
                   value_type_cstring(val_ident.type)), EMPTY_RAW_VAL.vat_t))

bool values_eq(Value a, Value b);
bool is_falsey(Value val);

char *value_type_cstring(ValueType type);
Str value_to_str(Value val);
void print_value(Value val);

#endif
