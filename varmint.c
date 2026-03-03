#include "error.h"
#include "disassemble.h"
#include "compile.h"
#include "varmint.h"
#include "vm.h"

#include <stdio.h>
#include <readline/readline.h>

// typeof(val) -> string
static Value _typeof(Varmint *vm, Value *args)
{
  Value val = args[0];
  const char *type_string = value_type_cstring(val.type);

  return String_create(vm, type_string, strlen(type_string));
}

// lenof(collection) -> number
static Value _lenof(Varmint *vm, Value *args)
{
  Value collection = args[0];

  switch (collection.type) {
  case V_string:
    return value_new((float64_t)collection.as.string->len, number);
  case V_list:
    return value_new((float64_t)collection.as.list->len, number);
  default:
    runtime_error(vm, "expect type string or list for collection, got %s",
        value_type_cstring(collection.type));
    return NO_VALUE;
  }
}

// put(output_string: string)
static Value _put(Varmint *vm, Value *args)
{
  Value output_string = args[0];

  String *s = typechecked(vm, output_string, string);
  printf("%.*s", (int)s->len, s->s);

  return NO_VALUE;
}

// putln(output_string: string)
static Value _putln(Varmint *vm, Value *args)
{
  Value output_string = args[0];

  String *s = typechecked(vm, output_string, string);
  printf("%.*s\n", (int)s->len, s->s);

  return NO_VALUE;
}

// input() -> string
static Value _input(Varmint *vm, Value *args)
{
  char *input_line = readline(NULL);
  return String_own(vm, input_line);
}

// to_number(val) -> maybe(number)
static Value _to_number(Varmint *vm, Value *args)
{
  float64_t n;

  Value val = args[0];

  switch (val.type) {
  case V_no:
    unreachable();
  case V_number:
    n = val.as.number;
    break;
  case V_boolean:
    n = val.as.boolean ? 1 : 0;
    break;
  case V_string:
    {
      char c = val.as.string->s[0];
      // man 3 strtod
      if (!isdigit(c)) switch (c) {
      case '+': case '-': // Optional sign
      case 'I': case 'i': // INFINITY
      case 'N': case 'n': // NAN
        break;
      default:
        // Invalid string!
        goto no_num;
      }

      char *endptr;
      n = strtod(val.as.string->s, &endptr);

      if (*endptr != '\0')
        // Invalid tailing characters!
        goto no_num;

      break;
    }
  case V_native:
  case V_maybe:
  case V_list:
  case V_procedure:
    goto no_num;
  }

  return Maybe_some(vm, value_new(n, number));
no_num:
  return Maybe_none(vm);
}

// rot(text: string, shift: number) -> string
static Value _rot(Varmint *vm, Value *args)
{
  Value shift = args[0],
        text = args[1];

  int shift_n = (int)typechecked(vm, shift, number);
  String *s = typechecked(vm, text, string);

  Value ciphertext = String_create(vm, s->s, s->len);

  // https://en.wikipedia.org/wiki/Caesar_cipher
  for (size_t i = 0; i < s->len; i++) {
    const char c = s->s[i];

    if (!isalpha(c))
      ciphertext.as.string->s[i] = c;

    else {
      char ciphered_c = (((toupper(c) - 'A') + shift_n) % 26) + 'A';
      if (islower(c))
        ciphered_c = (char)tolower(ciphered_c);

      ciphertext.as.string->s[i] = ciphered_c;
    }
  }

  return ciphertext;
}

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

  vm.natives = Natives_init();
  vm.natives_table = NativesTable_init();
  // Initialize native functions.
  varmint_add_native(&vm, "typeof", _typeof, 1);
  varmint_add_native(&vm, "lenof", _lenof, 1);
  varmint_add_native(&vm, "put", _put, 1);
  varmint_add_native(&vm, "putln", _putln, 1);
  varmint_add_native(&vm, "input", _input, 0);
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
    printf("*** TOKENS ***\n");

    Lex l = lex_new(source);

    Token tok;
    do {
      tok = lex_token(&l);
      printf("%.2li %s `%.*s`\n",
          tok.line,
          token_cstring(tok.type),
          (int)tok.slice.len, tok.slice.s);
    } while (tok.type != TK_EOF);

    printf("\n");
  }
#endif

  Procedure *program = compile(vm, source);
  if (program == NULL)
    return NO_VALUE;

#ifdef VARMINT_DEBUG
  printf("*** INSTRUCTIONS ***\n");
  disassemble(program);
  printf("\n");
#endif

  execute(vm, program);
  return vm->result;
}
