#include "../inc/dis.h"
#include "../inc/compile.h"
#include "../inc/internal.h"
#include "../inc/varmint.h"
#include "../inc/vm.h"

#include <stdio.h>
#include <readline/readline.h>

void varmint_add_native(Varmint *vm,
    const char *name, NativeFn fn, size_t arity)
{
  Str name_str = str_new(name, strlen(name));

  Native native = {arity, fn, name_str};
  Natives_push(&vm->natives, native);

  size_t idx = vm->natives.len - 1;
  NativesTable_set(&vm->natives_table, name_str, idx);
}

Varmint varmint_start(void)
{
  Varmint vm;

  vm.op_stack = OpStack_init();
  vm.call_stack = CallStack_init();
  vm.open_upvalues = NULL;

  vm.natives = Natives_init();
  vm.natives_table = NativesTable_init();
  // Initialize native functions.
  varmint_add_native(&vm, "typeof", _typeof, 1);
  varmint_add_native(&vm, "lenof", _lenof, 1);
  varmint_add_native(&vm, "put", _put, 1);
  varmint_add_native(&vm, "putln", _putln, 1);
  varmint_add_native(&vm, "input", _input, 0);
  varmint_add_native(&vm, "prompt", _prompt, 1);
  varmint_add_native(&vm, "to_number", _to_number, 1);
  varmint_add_native(&vm, "rot", _rot, 2);

  gc_init(&vm);

  return vm;
}

void varmint_free(Varmint *vm)
{
  gc_end(vm);
  free(vm->call_stack.data);
  // NB! Don't free op stack, it isn't dynamically allocated.
  free(vm->natives.data);
  free(vm->natives_table.entries);
}

Value varmint_run(Varmint *vm, char *source)
{
  vm->source = source;

#ifdef VARMINT_DEBUG
  {
    fprintf(stderr, "*** TOKENS ***\n");

    Lex l = lex_new(source);

    Token tok;
    do {
      tok = lex_token(&l);
      fprintf(stderr, "%.2li %s `%.*s`\n",
          tok.line,
          token_cstring(tok.type),
          (int)tok.slice.len, tok.slice.s);
    } while (tok.type != TK_EOF);

    fprintf(stderr, "\n");
  }
#endif

  Procedure *program = compile(vm, source);
  if (program == NULL)
    return NO_VALUE;

#ifdef VARMINT_DEBUG
  fprintf(stderr, "*** INSTRUCTIONS ***\n");
  dis(stderr, program, "program");
#endif

  execute(vm, program);
  return vm->result;
}
