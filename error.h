#ifndef LANG_ERROR_H
#define LANG_ERROR_H

#include "val.h"
#include "varmint.h"

#include <stdarg.h>

// Helper function for errors.
void error_out(const char *fmt, ...);
void v_error_out(const char *fmt, va_list args);

// Pause execution; print a stack trace and the error message.
void runtime_error(Varmint *vm, const char *fmt, ...);

// Check type and return the unwrapped value
#define typechecked(vm, val_ident, vat_t) \
  (val_ident.type == VAL_##vat_t \
    ? val_ident.raw.vat_t \
    : (runtime_error(vm, \
        "Expect type " #vat_t " for " #val_ident ", got %s\n", \
                   value_type_cstring(val_ident.type)), EMPTY_RAW_VAL.vat_t))

void error_line_snip(char *source, size_t line, char *pointer_pos);

#endif
