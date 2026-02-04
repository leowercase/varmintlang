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

static inline bool execute_instruction(Varmint *vm, Proc *proc)
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
  case op_name: \
    { \
      uint8_t operand_ident = *vm->ip; \
      vm->ip++; \
      stmt; \
    } \
  case op_name##16: \
    { \
      uint16_t operand_ident = uint8_to_16(vm->ip); \
      vm->ip += 2; \
      stmt; \
    }

  switch ((int)instruction) {
  case OP_NOT:    UNARY(value_new((int)value_is_falsey(operand), boolean))
  case OP_NEGATE: UNARY(_vm_negate(operand))

  case OP_FACTORIAL:  UNARY(_vm_factorial(operand))
  case OP_PERCENTAGE: UNARY(_vm_percentage(operand))

  case OP_ADD: BINARY(_vm_add(lhs, rhs))
  case OP_SUB: BINARY(_vm_subtract(lhs, rhs))
  case OP_MUL: BINARY(_vm_multiply(lhs, rhs))
  case OP_DIV: BINARY(_vm_divide(lhs, rhs))

  case OP_POW:    BINARY(_vm_pow(lhs, rhs))
  case OP_MODULO: BINARY(_vm_modulo(lhs, rhs))

  case OP_AND: BINARY(_vm_and(lhs, rhs))
  case OP_OR:  BINARY(_vm_or(lhs, rhs))
  case OP_I9N: BINARY(_vm_implies(lhs, rhs))

  case OP_EQ:  BINARY(value_new((int)values_eq(lhs, rhs), boolean))
  case OP_NEQ: BINARY(value_new((int)!values_eq(lhs, rhs), boolean))

  case OP_LT:  BINARY(_vm_less_than(lhs, rhs))
  case OP_GT:  BINARY(_vm_greater_than(lhs, rhs))
  case OP_LEQ: BINARY(_vm_less_than_or_eq(lhs, rhs))
  case OP_GEQ: BINARY(_vm_greater_than_or_eq(lhs, rhs))

  case OP_CONCAT: BINARY(_vm_concat(lhs, rhs))

  case_size_op(OP_CONST, idx,
    {
      Value constant = proc->code.constants.data[idx];
      push(vm, constant);
      break;
    })

  case OP_ONE:
    {
      const Value one = value_new(1.0, number);
      push(vm, one);
      break;
    }

    // Weaves a list.
  case_size_op(OP_BUILD_LIST, len,
    {
      ValueList list = ValueList_with_cap(len);
      list.len = len;

      for (int i = len - 1; i >= 0; i--)
        list.data[i] = pop(vm);

      Value val = heaped_value_new(ValueList, list);
      *val.raw.list = list;

      push(vm, val);
      break;
    })

    // Stitches together the metastrings emitted by the compiler.
  case_size_op(OP_BUILD_STR, metastrs,
    {
      Str str = value_to_str(pop(vm));

      for (int i = 1; i < metastrs; i++)
        str = str_concat(value_to_str(pop(vm)), str);

      push(vm, string_value_new(str));
      break;
    })

    // Chains the right hand side of an op to be the left hand of another.
    // The good ol' switcheroo.
  case OP_CHAIN_BINOP:
    {
      Value rhs = peek(vm);
      bool running = execute_instruction(vm, proc);
      push(vm, rhs);
      return running;
    }

  case_size_op(OP_GET, stack_slot,
    {
      push(vm, vm->stack.data[stack_slot]);
      break;
    })
  case_size_op(OP_SET, stack_slot,
    {
      vm->stack.data[stack_slot] = peek(vm);
      break;
    })
  case_size_op(OP_DISCARD_SET, stack_slot,
    {
      vm->stack.data[stack_slot] = pop(vm);
      break;
    })

  case OP_RESERVE_SLOT:
    push(vm, NO_VAL);
    break;
  case OP_DUPLICATE:
    push(vm, peek(vm));
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

  default: unreachable();
  }

  return true;

#undef UNARY
#undef BINARY
#undef case_size_op
}

void run_proc(Varmint *vm, Proc *proc)
{
  vm->ip = proc->code.instruc.data;

  bool running;
  do
    running = execute_instruction(vm, proc);
  while (running);
}
