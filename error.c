#include "varmint.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>

void v_error_out(const char *fmt, va_list args)
{
  fprintf(stderr, ANSI_RED);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ANSI_RESET);
}

void error_out(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  v_error_out(fmt, args);
  va_end(args);
}

// Print line snippet, optionally with a pointer ^
void error_line_snip(char *source, size_t line, char *pointer_pos)
{
  char *s = source;
  size_t len = 0;

  // Find line string and calculate its length
  for (size_t lines_traversed = 0; *s != '\0'; s++) {
    if (line - 1 == lines_traversed) {
      // This is the line!
      for (; s[len] != '\n'; len++);
      break;
    }
    if (*s == '\n') lines_traversed++;
  }

  fprintf(stderr,
      ANSI_WHITE "\t%.*s\n" ANSI_RESET, (int)len, s);

  if (pointer_pos != NULL) {
    int column = (int)(pointer_pos - s);
    error_out("\t%*s^", column, "");
  }
  error_out("\n");
}

void runtime_error(Varmint *vm, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  v_error_out(fmt, args);
  va_end(args);

  for (CallFrame *frame = CallStack_top(&vm->call_stack);
      frame >= vm->call_stack.data; frame--) {
    Proc *procedure = frame->procedure;

    size_t offset = (size_t)(frame->ip - procedure->code.instructions.data),
           line = get_line(&procedure->code.lines, offset);

    error_out("[line %li] in ", line);

    if (procedure->name.len > 0)
      error_out("%.*s:\n",
          (int)procedure->name.len, procedure->name.s);
    else if (frame == vm->call_stack.data)
      error_out("program:\n");
    else
      error_out("anonymous function:\n");

    error_line_snip(vm->source, line, NULL);
  }

  exit(EX_DATAERR);
}
