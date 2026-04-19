#include "../inc/compile.h"
#include "../inc/internal.h"
#include "../inc/varmint.h"
#include "../inc/vm.h"

#include <stdio.h>
#include <readline/readline.h>

Varmint varmint_start(void)
{
  Varmint vm;

  vm.op_stack = OpStack_init();
  vm.call_stack = CallStack_init();
  vm.open_upvalues = NULL;

  gc_init(&vm);

  vm.result = NO_VALUE;
  vm.status = VM_A_OK;

  return vm;
}

void varmint_free(Varmint *vm)
{
  gc_end(vm);
  free(vm->call_stack.data);
  // NB! Don't free op stack, it isn't dynamically allocated.
}

VarmintStatus varmint_run(Varmint *vm, String *source)
{
  return varmint_run_with(vm, NULL, true, source);
}

VarmintStatus varmint_run_with(Varmint *vm,
    Parse *parse, bool discard_parse_state, String *source)
{
  Procedure *program = compile(
      vm, parse, discard_parse_state, source);

  if (program != NULL) {
    call_program(vm, program);
    run_bytecode(vm);
  }

  return vm->status;
}
