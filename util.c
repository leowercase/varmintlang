#include "util.h"

#include <stdarg.h>
#include <stdio.h>

void error_out(const char *fmt, ...)
{
  va_list args;
  fprintf(stderr, ANSI_RED);

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  fprintf(stderr, ANSI_RESET);
}
