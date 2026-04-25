#include "../inc/compile.h"
#include "../inc/internal.h"
#include "../inc/varmint.h"
#include "../inc/vm.h"

#include <stdio.h>
#include <readline/readline.h>

#include <math.h>

static const NameValue default_builtins[] = {
  { str_from("e"),         value_new(VARMINT_E,   number) },
  { str_from("pi"),        value_new(VARMINT_PI,  number) },
  { str_from("tau"),       value_new(VARMINT_TAU, number) },

  { str_from("typeof"),    value_new(_typeof,     native) },
  { str_from("len"),       value_new(_len,        native) },

  { str_from("to_number"), value_new(_to_number,  native) },
  { str_from("to_string"), value_new(_to_string,  native) },
  { str_from("unwrap"),    value_new(_unwrap,     native) },

  { str_from("put"),       value_new(_put,        native) },
  { str_from("putln"),     value_new(_putln,      native) },
  { str_from("input"),     value_new(_input,      native) },

  { str_from("time"),      value_new(_time,       native) },

  { str_from("rot"),       value_new(_rot,        native) },
};
static const size_t default_builtin_count =
  sizeof(default_builtins) / sizeof(NameValue);

Varmint varmint_start(void)
{
  Varmint vm;

  vm.op_stack = OpStack_init();
  vm.call_stack = CallStack_init();
  vm.open_upvalues = NULL;

  gc_init(&vm);

  vm.result = NO_VALUE;
  vm.status = VM_A_OK;

  // Initialize builtins.
  vm.builtins = NameValues_with_cap(default_builtin_count);

  for (size_t i = 0; i < default_builtin_count; i++)
    NameValues_push(&vm.builtins, default_builtins[i]);

  return vm;
}

void varmint_free(Varmint *vm)
{
  gc_end(vm);
  free(vm->call_stack.data);
  free(vm->builtins.data);
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
