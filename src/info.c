#include "../inc/varmint.h"
#include "../inc/util.h"

#include <stdarg.h>
#include <stdio.h>

size_t get_line(LineInfo *lines, size_t offset)
{
  for (size_t i = 0; i < lines->len; i++) {
    LineBytes l = lines->data[i];

    if (offset <= l.nbytes)
      return l.line;

    offset -= l.nbytes;
  };
  abort(); // Unreachable, assuming well-formed line info
}

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

void info_out(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  fprintf(stderr, ANSI_YELLOW);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ANSI_RESET);
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
    info_out("\t%*s^", column, "");
  }
  error_out("\n");
}

static const uint8_t HALT_INSTRUCTION = OP_HALT;

void runtime_error(Varmint *vm, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  v_error_out(fmt, args);
  va_end(args);
  error_out("\n");

  for (CallFrame *frame = CallStack_top(&vm->call_stack);
      frame >= vm->call_stack.data; frame--) {
    Procedure *proc = frame->procedure;

    size_t offset = (size_t)(frame->ip - proc->code.instructions.data),
           line = get_line(&proc->code.lines, offset);

    error_out("[line %li] in ", line);

    if (proc->name.len > 0)
      error_out("%.*s:\n", (int)proc->name.len, proc->name.s);
    else if (frame == vm->call_stack.data)
      error_out("program:\n");
    else
      error_out("anonymous function:\n");

    error_line_snip(proc->source->s, line, NULL);
  }

#ifdef VARMINT_DEBUG
  info_out("operation stack:\n");

  for (size_t i = 0; i < vm->op_stack.len; i++) {
    info_out("[%li] ", i);
    print_value(stderr, vm->op_stack.data[i]);
    info_out("\n");
  }
#endif

  vm->status = VM_RUNTIME_ERR;
  vm->frame->ip = &HALT_INSTRUCTION;
}
