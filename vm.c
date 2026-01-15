#include "internal.h"
#include "util.h"
#include "val.h"
#include "vm.h"

static inline void push(Varmint *vm, Value value)
{
  Stack_push(&vm->stack, value);
}

static inline Value pop(Varmint *vm)
{
  return Stack_pop(&vm->stack);
}

static inline Value peek(Varmint *vm)
{
  return Stack_top(&vm->stack);
}

static inline bool execute_instruction(Varmint *vm, PCode *code)
{
  Opcode instruction = *(vm->ip++);

  // Macros really help with some of the tedium here.

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

  // Opcode with variable sized operand (8/16-bit)
#define case_size_op(op_name, operand_ident, stmt) \
  case op_name: { \
    uint8_t operand_ident = *vm->ip; \
    vm->ip++; \
    stmt; \
  } \
  case op_name##16: { \
    uint16_t operand_ident = uint8_to_16(vm->ip); \
    vm->ip += 2; \
    stmt; \
  }

  switch (instruction) {
  case OP_NONE:
    abort(); // Unreachable

  case_size_op(OP_CONST, idx,
    {
      Value constant = code->constants.data[idx];
      push(vm, constant);
      break;
    })

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

    // Weaves a list.
  case_size_op(OP_BUILD_LIST, len,
    {
      error_out("TODO!\n");
      abort();
    })

  case OP_TO_STR: UNARY(value_new(stringval_new(
                          value_to_str(operand)), string))
  case OP_CONCAT: BINARY(__vat_concat(lhs, rhs))

    // Stitches together the metastrings emitted by the compiler.
  case_size_op(OP_BUILD_STR, metastrs,
    {
      Str str = value_to_str(pop(vm));

      for (int i = 1; i < metastrs; i++)
        str = str_concat(value_to_str(pop(vm)), str);

      push(vm, value_new(stringval_new(str), string));
      break;
    })

  case OP_CHAIN_BINOP:
    {
      // The good ol' switcheroo.
      Value rhs = peek(vm);
      bool running = execute_instruction(vm, code);
      push(vm, rhs);
      return running;
    }

  case_size_op(OP_SET, stack_slot,
    {
      Value val;
      val = vm->stack.data[stack_slot] = pop(vm);
      push(vm, val);
      break;
    })

  case_size_op(OP_GET, stack_slot,
    {
      push(vm, vm->stack.data[stack_slot]);
      break;
    })

  case OP_RESERVE_SLOT:
    push(vm, NO_VAL);
    break;
  case OP_DISCARD:
    pop(vm);
    break;
  case_size_op(OP_DISCARDN, n,
    {
      for (int i = 0; i < n; i++)
        pop(vm);

      break;
    })
  case_size_op(OP_RETAIN1_DISCARDN, n,
    {
      Value retained_val = pop(vm);

      for (int i = 1; i < n; i++)
        pop(vm);

      push(vm, retained_val);
      break;
    })

  case OP_RETURN:
    {
      vm->result =
        vm->stack.len == 0 ? NO_VAL : peek(vm);
      return false;
    }
  }

  return true;

#undef UNARY
#undef BINARY
#undef case_size_op
}

void run(Varmint *vm, PCode *code)
{
  vm->ip = code->instruc.data;

  bool running;
  do
    running = execute_instruction(vm, code);
  while (running);
}
