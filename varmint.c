#include "disassemble.h"
#include "compile.h"
#include "varmint.h"
#include "vm.h"

#include <stdio.h>
#include <readline/readline.h>

// typeof(val) -> string
static Value _typeof(Value *args)
{
  Value val = args[0];
  char *type = value_type_cstring(val.type);

  return string_value_new(str_new(type, strlen(type)));
}

// lenof(collection) -> number
static Value _lenof(Value *args)
{
  Value collection = args[0];

  switch (collection.type) {
  case VAL_string:
    return value_new((float64_t)collection.raw.string->str.len, number);
  case VAL_list:
    return value_new((float64_t)collection.raw.list->len, number);
  default:
    runtime_error("expect type string or list for collection, got %s",
        value_type_cstring(collection.type));
  }
}

// put(output_string: string)
static Value _put(Value *args)
{
  Value output_string = args[0];

  Str str = typechecked(output_string, string)->str;

  printf("%.*s", (int)str.len, str.s);

  return NO_VAL;
}

// input() -> string
static Value _input(Value *args)
{
  char *input_line = readline(NULL);

  Str str = str_new(input_line, strlen(input_line));
  return string_value_new(str);
}

// rot(text: string, shift: number) -> string
static Value _rot(Value *args)
{
  Value shift = args[0],
        text = args[1];

  int shift_n = (int)typechecked(shift, number);
  Str str = typechecked(text, string)->str;

  char *ciphertext = allocate(NULL, sizeof(str) + sizeof('\0'));
  ciphertext[str.len] = '\0';

  // https://en.wikipedia.org/wiki/Caesar_cipher
  for (size_t i = 0; i < str.len; i++) {
    const char c = str.s[i];

    if (!isalpha(c))
      ciphertext[i] = c;

    else {
      char ciphered_c = (((toupper(c) - 'A') + shift_n) % 26) + 'A';
      if (islower(c))
        ciphered_c = (char)tolower(ciphered_c);

      ciphertext[i] = ciphered_c;
    }
  }

  return string_value_new(str_new(ciphertext, str.len));
}

Varmint varmint_start(void)
{
  Varmint vm;

  vm.op_stack = OpStack_new();
  vm.call_stack = CallStack_new();

  vm.natives = NativesTable_new();
  // Initialize native functions.
  add_native_fn(&vm, "typeof", _typeof, 1);
  add_native_fn(&vm, "lenof", _lenof, 1);
  add_native_fn(&vm, "put", _put, 1);
  add_native_fn(&vm, "input", _input, 0);
  add_native_fn(&vm, "rot", _rot, 2);

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

  Proc *program = compile(vm, source);
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
