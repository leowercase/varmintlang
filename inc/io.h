#ifndef VARMINT_IO
#define VARMINT_IO

#include <stdarg.h>

struct Varmint;

// The output functions are expected to properly handle
// ANSI escape codes and newlines "\n".

// Print printf-formatted output
typedef void (*VmPrint)(const char *fmt, ...);

// Print printf-formatted output, take in a `va_list`
typedef void (*VmVaPrint)(const char *fmt, va_list ap);

// Display the prompt and return input from the user
typedef struct String *(*VmInput)(struct Varmint *vm, const char *prompt);


#endif
