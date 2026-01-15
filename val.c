#include "val.h"

#include <stdio.h>

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
    case VAL_no:
      abort(); // Unreachable, if all is well.
    case VAL_number:
      return a.raw.number == b.raw.number;
    case VAL_boolean:
      return a.raw.boolean == b.raw.boolean;
    case VAL_string:
    case VAL_list:
      {
        error_out("TODO!\n");
        abort();
      }
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
  CASE(list)
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
      Str str = str_fmt("%g", val.raw.number);
      if (str.s == NULL) {
        error_out("Failed to convert number to string\n");
        exit(EX_OSERR);
      }
      return str;
    }
  case VAL_boolean:
    return str_from(val.raw.boolean ? "True" : "False");
  case VAL_string:
    return val.raw.string->str;
  case VAL_list:
    {
      List *list = &val.raw.list->list;
      Str str = str_fmt("[%s", value_to_str(list->data[0]).s);

      for (size_t i = 1; i < list->len - 1; i++) {
        str = str_fmt("%s, %s",
                      str.s, value_to_str(list->data[i]).s);
      }

      str = str_fmt("%s]", str.s);
      return str;
    }
  }
}

void print_value(Value val)
{
  switch (val.type) {
  case VAL_no:
    printf(ANSI_WHITE "no value" ANSI_RESET);
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
    printf("\"%s\"", val.raw.string->str.s);
    printf(ANSI_RESET);
    printf("(%li)", val.raw.string->str.len);
    break;
  case VAL_list:
    {
      List *list = &val.raw.list->list;
      printf(ANSI_WHITE "[");
      for (size_t i = 0; i < list->len; i++)
      {
        print_value(list->data[i]);
        if (i < list->len - 1)
          printf(", ");
      }
      printf(ANSI_WHITE "]" ANSI_RESET);
    }
  }
}
