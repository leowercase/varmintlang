#include "util.h"

#include <stdarg.h>
#include <stdio.h>

void v_error_out(const char *fmt, va_list args)
{
  fprintf(stderr, ANSI_RED);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ANSI_RESET);
}

void error_out(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  v_error_out(fmt, args);
  va_end(args);
}
