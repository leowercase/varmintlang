#ifndef LANG_INFO_H
#define LANG_INFO_H

#include "val.h"
#include "varmint.h"

#include <stdarg.h>

/*
 * Lines are run-length encoded to save memory.
 * This makes line info a bit slow to emit, but it only happens on errors.
 * https://en.wikipedia.org/wiki/Run-length_encoding
 */
size_t get_line(LineInfo *l, size_t instruction_idx);

// Helper function for errors.
void error_out(const char *fmt, ...);
void v_error_out(const char *fmt, va_list args);

// Info messages.
void info_out(const char *fmt, ...);

// Pause execution; print a stack trace and the error message.
void runtime_error(Varmint *vm, const char *fmt, ...);

// Check type and return the unwrapped value
#define typechecked(vm, val_ident, t) \
  (val_ident.type == V_##t \
    ? val_ident.as.t \
    : (runtime_error(vm, \
        "Expect type " #t " for " #val_ident ", got %s\n", \
                   value_type_cstring(val_ident.type)), (Valueu){0}.t))

void error_line_snip(char *source, size_t line, char *pointer_pos);

#endif
