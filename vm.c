#include "internal.h"
#include "util.h"
#include "val.h"
#include "vm.h"

VM vm_new()
{
  VM vm;
  vm.ip = NULL;
  vm.stack = Stack_new();
  return vm;
}

static inline void push(VM *vm, Value value)
{
  Stack_push(&vm->stack, value);
}

static inline Value pop(VM *vm)
{
  return Stack_pop(&vm->stack);
}

static inline Value peek(VM *vm)
{
  return Stack_top(&vm->stack);
}

static void build_list(VM *vm, size_t len)
{
  error_out("TODO!\n");
  abort();
}

// Stitch together the metastrings emitted by the compiler
static Value build_string(VM *vm, int substrs)
{
  Str str = value_to_str(pop(vm));

  for (int i = 1; i < substrs; i++)
    str = str_concat(value_to_str(pop(vm)), str);

  return value_new(str, string);
}

static inline bool execute_instruction(VM *vm, PCode *code)
{
  Opcode instruction = *(vm->ip++);

#define UNARY(expr) { \
  Value operand = pop(vm); \
  push(vm, (expr)); \
  break; \
}
#define BINARY(expr) { \
  Value rhs = pop(vm); \
  Value lhs = pop(vm); \
  push(vm, (expr)); \
  break; \
}

  switch (instruction) {
  case OP_NONE:
    abort(); // Unreachable

  case OP_CONST:
    {
      Value constant = code->constants.data[*(vm->ip++)];
      push(vm, constant);
      break;
    }
  case OP_CONST16:
    {
      uint16_t idx = uint8_to_16(vm->ip);
      Value constant = code->constants.data[idx];
      push(vm, constant);
      vm->ip += 2;
      break;
    }

  case OP_NOT:    UNARY(value_new((int)is_falsey(operand), boolean))
  case OP_NEGATE: UNARY(__vat_negate(operand))

  case OP_FACTORIAL:  UNARY(__vat_factorial(operand))
  case OP_PERCENTAGE: UNARY(__vat_percentage(operand))

  case OP_ADD: BINARY(__vat_add(lhs, rhs))
  case OP_SUB: BINARY(__vat_subtract(lhs, rhs))
  case OP_MUL: BINARY(__vat_multiply(lhs, rhs))
  case OP_DIV: BINARY(__vat_divide(lhs, rhs))

  case OP_POW:    BINARY(__vat_pow(lhs, rhs))
  case OP_MODULO: BINARY(__vat_modulo(lhs, rhs))

  case OP_AND: BINARY(__vat_and(lhs, rhs))
  case OP_OR:  BINARY(__vat_or(lhs, rhs))
  case OP_I9N: BINARY(__vat_implies(lhs, rhs))

  case OP_EQ:  BINARY(value_new((int)values_eq(lhs, rhs), boolean))
  case OP_NEQ: BINARY(value_new((int)!values_eq(lhs, rhs), boolean))

  case OP_LT:  BINARY(__vat_less_than(lhs, rhs))
  case OP_GT:  BINARY(__vat_greater_than(lhs, rhs))
  case OP_LEQ: BINARY(__vat_less_than_or_eq(lhs, rhs))
  case OP_GEQ: BINARY(__vat_greater_than_or_eq(lhs, rhs))

  case OP_BUILD_LIST:
    {
      uint8_t len = *(vm->ip++);
      build_list(vm, len);
      break;
    }
  case OP_BUILD_LIST16:
    {
      uint16_t len = uint8_to_16(vm->ip);
      vm->ip += 2;
      build_list(vm, len);
      break;
    }

  case OP_TO_STR: UNARY(value_new(value_to_str(operand), string))
  case OP_CONCAT: BINARY(__vat_concat(lhs, rhs))

  case OP_BUILD_STR:
    {
      uint8_t substrs = *(vm->ip++);
      push(vm, build_string(vm, substrs));
      break;
    }
  case OP_BUILD_STR16:
    {
      uint16_t substrs = uint8_to_16(vm->ip);
      vm->ip += 2;
      push(vm, build_string(vm, substrs));
      break;
    }

  case OP_CHAIN_BINOP:
    {
      // The good ol' switcheroo.
      Value rhs = peek(vm);
      bool running = execute_instruction(vm, code);
      push(vm, rhs);
      return running;
    }

  case OP_DISCARD:
    pop(vm);
    break;

  case OP_RETURN:
    {
      vm->result = pop(vm);
      return false;
    }
  }

  return true;

#undef UNARY_FN
#undef UNARY_OP
#undef BINARY_FN
#undef BINARY_OP
}

Value vm_run(VM *vm, PCode *code)
{
  vm->ip = code->instruc.data;

  bool running;
  do
    running = execute_instruction(vm, code);
  while (running);

  return vm->result;
}

void vm_free(VM *vm)
{
  free(vm->stack.data);
}
