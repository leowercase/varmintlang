#include "compile.h"
#include "disassemble.h"
#include "parse.h"
#include "varmint.h"
#include "vm.h"

Varmint varmint_start()
{
  Varmint vm;
  vm.stack = Stack_new();
  return vm;
}

void varmint_free(Varmint *vm)
{
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

  Tnode *ast = stmt(&p);

#ifdef VARMINT_DEBUG
  printf("*** AST ***\n");
  //treenode_print(ast);
  printf("\n\n");
#endif

  Proc program = compile(ast);

  // TODO
  return NO_VAL;

#ifdef VARMINT_DEBUG
  printf("*** INSTRUCTIONS ***\n");
  disassemble(&program.code);
  printf("\n");
#endif

  run_proc(vm, &program);
  return vm->result;
}
