#include "util.h"

#include <stdarg.h>
#include <stdio.h>

void error_out(const char *msg_template, ...)
{
  va_list args;
  fprintf(stderr, ANSI_RED);

  va_start(args, msg_template);
  vfprintf(stderr, msg_template, args);
  va_end(args);

  fprintf(stderr, ANSI_RESET);
}
