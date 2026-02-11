#include "disassemble.h"
#include "compile.h"
#include "varmint.h"
#include "vm.h"
#include <stdio.h>

Varmint varmint_start(void)
{
  Varmint vm;
  vm.op_stack = OpStack_new();
  vm.call_stack = CallStack_new();
  return vm;
}

void varmint_free(Varmint *vm)
{
  free(vm->call_stack.data);
}

Value varmint_run(Varmint *vm, char *source)
{
#ifdef VARMINT_DEBUG
  {
    printf("*** TOKENS ***\n");

    Lex l = lex_new(source);

    Token tok;
    do {
      tok = lex_token(&l);
      printf("%.2li %s `%.*s`\n",
          tok.line,
          tok_cstring(tok.type),
          (int)tok.slice.len, tok.slice.s);
    } while (tok.type != TK_EOF);

    printf("\n");
  }
#endif

  Proc *program = compile(source);
  if (program == NULL)
    return NO_VAL;

#ifdef VARMINT_DEBUG
  printf("*** INSTRUCTIONS ***\n");
  disassemble(program);
  printf("\n");
#endif

  execute(vm, program);
  return vm->result;
}
