#include "disassemble.h"
#include "parse.h"
#include "varmint.h"
#include "vm.h"

Varmint varmint_start()
{
  FnScope scope;
  scope.locals = Locals_new();
  scope.depth = 0;
  scope.enclosing_scope = NULL;

  Varmint vm;
  vm.current_scope = scope;
  vm.stack = Stack_new();
  return vm;
}

void varmint_free(Varmint *vm)
{
  free(vm->current_scope.locals.data);
  free(vm->stack.data);
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
      printf("%.2li %s `%.*s`\n", tok.line, tok_cstring(tok.type),
          (int)tok.slice.len, tok.slice.s);
    } while (tok.type != TK_EOF);

    printf("\n");
  }
#endif

  Parse p = init_parse(vm, source);

  stmt(&p);
  emit_byte(&p.code, p.code.lines.len, OP_RETURN);

#ifdef VARMINT_DEBUG
  printf("*** INSTRUCTIONS ***\n");
  disassemble(&p.code);
  printf("\n");
#endif

  run_code(vm, &p.code);
  return vm->result;
}
