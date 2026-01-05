#include "val.h"

#include <stdio.h>

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
      case VAL_number:
        return a.raw.number == b.raw.number;
      case VAL_boolean:
        return a.raw.boolean == b.raw.boolean;
      case VAL_string:
        error_out("TODO!\n");
        abort();
    }
  }
}

bool is_falsey(Value val)
{
  if (is_type(val, boolean))
    return !val.raw.boolean;
  else
    return false;
}

const char *val_type_cstring(const ValueType type)
{
#define CASE(name) case VAL_##name: return #name;

  switch (type) {
  CASE(number)
  CASE(boolean)
  CASE(string)
  }

#undef CASE
}

void print_value(Value val)
{
  switch(val.type) {
  case VAL_number:
    printf(ANSI_RED);
    printf("%g", val.raw.number);
    printf(ANSI_RESET);
    break;
  case VAL_boolean:
    printf(ANSI_BLUE);
    printf("%s", val.raw.boolean ? "True" : "False");
    printf(ANSI_RESET);
    break;
  case VAL_string:
    printf(ANSI_YELLOW);
    printf("\"%.*s\"", (int)val.raw.string.len, val.raw.string.s);
    printf(ANSI_RESET);
    break;
  }
}
