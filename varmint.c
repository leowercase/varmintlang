#include "disassemble.h"
#include "parse.h"
#include "varmint.h"
#include "vm.h"
#include <stdio.h>

static void reset_code(Varmint *vm)
{

}

Varmint varmint_start()
{
  Varmint vm;
  vm.op_stack = OpStack_new();
  return vm;
}

void varmint_free(Varmint *vm)
{
  free(vm->op_stack.data);
}

Value varmint_run(Varmint *vm, char *source)
{
  reset_code(vm);

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

  Proc program = parse_stmt(vm, source);
  // TODO

#ifdef VARMINT_DEBUG
  printf("*** INSTRUCTIONS ***\n");
  disassemble(&program.code);
  printf("\n");
#endif

  return execute(vm);
}
