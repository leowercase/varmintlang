#ifndef LANG_UTIL_H
#define LANG_UTIL_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sysexits.h>

// ANSI escape codes control styling in terminals
// https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
#define ANSI_BLACK   "\x1b[30m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_WHITE   "\x1b[37m"
#define ANSI_RESET   "\x1b[0m"

// Helper function for errors.
void error_out(const char *msg_template, ...);
void runtime_error(const char *msg_template, ...);

// Two passes of macro expansion are required for macro identifiers to expand
#define CONCAT(a, b) a##b
#define JOIN(a, b) CONCAT(a, b)

static_assert(sizeof(double) == 8 * sizeof(uint8_t), "Expect 64-bit double.");
#define float64_t double

#define uint8_to_16(uints) \
  (((uints)[0] << 8) | (uints)[1])

#define uint16_to_8(uint) \
  {((uint) & 0xff00) >> 8, (uint) & 0x00ff}

#endif
