#include "error.h"
#include "val.h"
#include "proc.h"
#include "state.h"

#include <stdio.h>

bool values_eq(Value a, Value b)
{
  if (a.type != b.type) return false;
  else {
    switch (a.type) {
    case VAL_no: unreachable();
    case VAL_number:
      return a.raw.number == b.raw.number;
    case VAL_boolean:
      return a.raw.boolean == b.raw.boolean;
    case VAL_native:
      return a.raw.native->fn == b.raw.native->fn;
    case VAL_string:
      return strs_eq(a.raw.string->str, b.raw.string->str);
    case VAL_list:
    case VAL_function:
      {
        error_out("TODO!\n");
        abort();
      }
    case VAL_program:
      unreachable();
    }
  }
}

bool value_is_falsey(Value val)
{
  if (val.type == VAL_boolean)
    return !val.raw.boolean;
  else
    return false;
}

char *value_type_cstring(ValueType type)
{
#define case_(name) case VAL_##name: return #name;

  switch (type) {
  case VAL_no:
    return "no value";
  case_(number)
  case_(boolean)
  case_(native)
  case_(string)
  case_(list)
  case_(function)
  case_(program)
  }

#undef case_
}

Str value_to_str(Value val)
{
  switch (val.type) {
  case VAL_no:
    return str_from(NULL);
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
    return val.raw.boolean ? str_from("True") : str_from("False");
  case VAL_string:
    return val.raw.string->str;
  case VAL_list:
    {
      ValueList *list = val.raw.list;
      Str str = str_fmt("[%s", value_to_str(list->data[0]).s);

      for (size_t i = 1; i < list->len - 1; i++) {
        str = str_fmt("%s, %s",
                      str.s, value_to_str(list->data[i]).s);
      }

      str = str_fmt("%s]", str.s);
      return str;
    }
  case VAL_function:
    {
      Str name = val.raw.function->name;
      if (name.s != NULL)
        return str_fmt("<fn %.*s>", (int)name.len, name.s);
      else
        return str_from("<fn>");
    }
  case VAL_native:
    return str_from("<native fn>");
  case VAL_program:
    return str_from("<program>");
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
      ValueList *list = val.raw.list;
      printf(ANSI_MAGENTA "[");
      for (size_t i = 0; i < list->len; i++) {
        print_value(list->data[i]);
        if (i < list->len - 1)
          printf(", ");
      }
      printf(ANSI_MAGENTA "]" ANSI_RESET);
      break;
    }
  case VAL_function:
    {
      Str name = val.raw.function->name;
      printf(ANSI_GREEN);
      if (name.s != NULL)
        printf("<fn %.*s>", (int)name.len, name.s);
      else
        printf("<fn>");
      printf(ANSI_RESET);
      break;
    }
  case VAL_native:
    printf(ANSI_GREEN "<native fn>" ANSI_RESET); break;
  case VAL_program:
    printf(ANSI_GREEN "<program>" ANSI_RESET); break;
  }
}
