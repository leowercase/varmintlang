#include "../inc/varmint.h"
#include "../inc/util.h"

#include <stdarg.h>
#include <stdio.h>

size_t get_line(LineInfo *lines, size_t offset)
{
  // The amount of bytes emitted up to and including this byte
  size_t bytes = offset + 1;

  // Look for the line where
  //   n ∈ (Σ of bytes emitted before line, Σ of bytes emitted after line]
  for (size_t i = 0; i < lines->len; i++) {
    LineBytes entry = lines->data[i];

    if (bytes <= entry.bytes)
      // Current byte originated from this line!
      return entry.line;

    bytes -= entry.bytes;
  };
  assert(false); // Unreachable, assuming well-formed line info
}

// Print line snippet, optionally with a pointer ^
void error_line_snip(Varmint *vm,
    char *source, size_t line, char *pointer_pos)
{
  char *s = source;
  size_t len = 0;

  // Find line string and calculate its length
  for (size_t lines_traversed = 0; *s != '\0'; s++) {
    if (line - 1 == lines_traversed) {
      // This is the line!
      for (; s[len] != '\n' && s[len] != '\0'; len++);
      break;
    }
    if (*s == '\n') lines_traversed++;
  }

  vm->io.info(ANSI_WHITE "\t%.*s\n" ANSI_RESET, (int)len, s);

  if (pointer_pos != NULL) {
    int column = (int)(pointer_pos - s);
    vm->io.info("\t%*s^\n", column, "");
  }
}

void print_op_stack(Varmint *vm)
{
  vm->io.info("operation stack:\n");

  for (size_t i = 0; i < vm->op_stack.len; i++) {
    vm->io.info("[%li] ", i);
    print_value(vm->io.info, vm->op_stack.data[i]);
    vm->io.info("\n");
  }
}

static const uint8_t HALT_INSTRUCTION = OP_HALT;

void runtime_error(Varmint *vm, const char *fmt, ...)
{
  if (vm->status == VM_RUNTIME_ERR) return;

  va_list args;

  va_start(args, fmt);
  vm->io.va_error(fmt, args);
  va_end(args);

  vm->io.error("\n");

  // Stack trace.
  for (CallFrame *frame = CallStack_top(&vm->call_stack);;) {
    Procedure *proc = frame->procedure;

    // Step back by 1 to the bytes of the instruction at fault.
    size_t offset = (size_t)(frame->ip - proc->code.instructions.data - 1);

    size_t line = get_line(&proc->code.lines, offset);
    vm->io.error("[line %li] in ", line);

    if (proc->name.len > 0)
      vm->io.error("%.*s:\n", (int)proc->name.len, proc->name.s);
    else if (frame == vm->call_stack.data)
      vm->io.error("program:\n");
    else
      vm->io.error("anonymous function:\n");

    error_line_snip(vm, proc->source->s, line, NULL);

    if (--frame < vm->call_stack.data) break;
    else vm->io.error("\n");
  }

#ifdef VARMINT_DEBUG
  print_op_stack(vm);
#endif

  vm->status = VM_RUNTIME_ERR;
  vm->frame->ip = &HALT_INSTRUCTION;
}
