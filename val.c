#include "val.h"

#include <stdio.h>

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
      case VAL_no:
        abort();
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

char *value_type_cstring(ValueType type)
{
#define CASE(name) case VAL_##name: return #name;

  switch (type) {
  case VAL_no:
    return "no value";
  CASE(number)
  CASE(boolean)
  CASE(string)
  }

#undef CASE
}

Str value_to_str(Value val)
{
  switch (val.type) {
  case VAL_no:
    abort();
  case VAL_number:
    {
      Str string = str_fmt("%g", val.raw.number);
      if (string.s == NULL) {
        error_out("Failed to convert number to string\n");
        exit(EX_OSERR);
      }
      return string;
    }
  case VAL_boolean:
    return str_from(val.raw.boolean ? "True" : "False");
  case VAL_string:
    return val.raw.string;
  }
}

void print_value(Value val)
{
  switch (val.type) {
  case VAL_no:
    printf("no value");
    break;
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
    printf("\"%s\"", val.raw.string.s);
    printf(ANSI_RESET);
    printf("(%li)", val.raw.string.len);
    break;
  }
}
