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

uint16_t uint8_to_16(uint8_t uints[2])
{
  return (uints[0] << 8) | uints[1];
}

void uint16_to_8(uint16_t uint, uint8_t uints[2])
{
  uints[0] = (uint & 0xff00) >> 8;
  uints[1] = uint & 0x00ff;
}

