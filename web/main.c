#include "../inc/compile.h"
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

EM_JS(char *, call_input, (const char *prompt), {
    const s = window.prompt(UTF8ToString(prompt));
    return stringToNewUTF8(s);
});

static String *input(Varmint *vm, const char *prompt)
{
  return String_own(vm, call_input(prompt)).as.string;
}

static const Varmio IO = {
  .out = print_out,
  .error = print_err,
  .va_error = va_print_err,
  .info = print_info,
  .input = input,
};

// Output color needs to be explicitly cleared with ANSI_RESET,
// otherwise the previous color "leaks"

void run(char *source)
{
  printf(ANSI_RESET);

  Varmint vm = varmint_init(IO);
  varmint_run(&vm,
      String_own(&vm, source).as.string);
}

void dis(char *source)
{
  printf(ANSI_RESET);

  Varmint vm = varmint_init(IO);
  varmint_dis(&vm, NULL, print_out,
      String_own(&vm, source).as.string, "program");
}
