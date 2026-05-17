#include "../inc/varmint.h"
#include <emscripten.h>

// IO impls

static void print_out(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
}

static void va_print_err(const char *fmt, va_list ap)
{
  fprintf(stderr, ANSI_RED);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, ANSI_RESET);
}

static void print_err(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  va_print_err(fmt, ap);
  va_end(ap);
}

static void print_info(const char *fmt, ...)
{
  fprintf(stderr, ANSI_YELLOW);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr, ANSI_RESET);
}

static String *input(Varmint *vm, const char *prompt)
{
  return String_own(vm, readline(prompt)).as.string;
}

char *test(const char *input)
{
  size_t len = strlen(input);

  char *s = malloc(len * sizeof(char));
  if (s == NULL) exit(1);

  for (size_t i = 0; i < len; i++)
    s[i] = input[i] + 1;

  return s;
}
