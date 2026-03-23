#ifndef VARMINT_UTIL_H
#define VARMINT_UTIL_H

#include <assert.h>
#include "stdarg.h"
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

// C11 shim.
// https://en.cppreference.com/w/c/program/unreachable
#if __STDC_VERSION__ < 202311L
#define unreachable() abort()
#endif

// Two passes of macro expansion are required for macro identifiers to expand
#define CONCAT(a, b) a##b
#define JOIN(a, b) CONCAT(a, b)

static_assert(sizeof(double) == 8 * sizeof(uint8_t), "Expect 64-bit double.");
typedef double float64_t;

#define uint8_to_16(uints) \
  (uint16_t)(((uints)[0] << 8) | (uints)[1])

#define uint16_to_8(uint) { \
  (uint8_t)(((uint) & 0xff00) >> 8), \
  (uint8_t)((uint) & 0x00ff), \
}

// Modulo is hecka slow (on modern computers).
// Bitwise AND on the other hand...
// a mod n = a & (n-1), ∃m∈ℕ(a = 2^m)
#define mod_2(a, n) ((a) & (n - 1))

#endif
