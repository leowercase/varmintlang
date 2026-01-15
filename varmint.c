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

Value varmint_go(Varmint *vm, char *source)
{
  // <DEBUG>
  {
    Lex l = lex_new(source);

    Token tok;
    do {
      tok = lex_token(&l);
      printf("%.2li %s `%.*s`\n", tok.line, tok_cstring(tok.type),
          (int)tok.slice.len, tok.slice.s);
    } while (tok.type != TK_EOF);
  }
  // </DEBUG>

  Lex lex = lex_new(source);

  Token current = lex_token(&lex);
  Token lookahead = lex_token(&lex);

  PCode code = new_p_code();
  Parse p = {lex, current, lookahead, code, &vm->current_scope};

  expr(&p, PREC_NONE);
  emit_byte(&p.code, p.code.lines.len - 1, OP_RETURN);

  // DEBUG
  disassemble(&p.code);

  run(vm, &p.code);
  return vm->result;
}
