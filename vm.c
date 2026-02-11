#include "internal.h"
#include "util.h"
#include "val.h"
#include "vm.h"

static inline void push(Varmint *vm, Value value)
{
  if (vm->op_stack.len >= OP_STACK_MAX)
    runtime_error("stack overflow\n");

  OpStack_push(&vm->op_stack, value);
}

static inline Value pop(Varmint *vm)
{
  return OpStack_pop(&vm->op_stack);
}

static inline void popn(Varmint *vm, size_t n)
{
  OpStack_popn(&vm->op_stack, n);
}

static inline Value peek(Varmint *vm)
{
  return *OpStack_top(&vm->op_stack);
}

static inline Value peeknth(Varmint *vm, size_t idx)
{
  return *(OpStack_top(&vm->op_stack) - idx);
}

// Call a function.
static void call(Varmint *vm, Proc *fn, size_t argc)
{
  // Create new frame for function call.
  CallFrame frame;
  frame.procedure = fn;
  frame.ip = fn->code.instructions.data;

  Value *top_slot = vm->op_stack.len == 0
    ? vm->op_stack.data : OpStack_push(&vm->op_stack, NO_VAL);

  frame.op_stack = top_slot - argc; // `argc` slots for parameters

  vm->frame = CallStack_push(&vm->call_stack, frame);
}

static void call_val(Varmint *vm, Value callee, size_t argc)
{
  Proc *fn = typechecked(callee, function);

  if (fn->arity != argc)
    runtime_error("expect %li parameters to %.*s but got %li\n",
        fn->arity, argc, fn->name);

  call(vm, fn, argc);
  pop(vm); // fn
}

static inline bool execute_instruction(Varmint *vm)
{
  Opcode instruction = *(vm->frame->ip++);

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
      uint8_t operand_ident = *vm->frame->ip; \
      vm->frame->ip++; \
      stmt; \
    } \
  case op_name##16: \
    { \
      uint16_t operand_ident = uint8_to_16(vm->frame->ip); \
      vm->frame->ip += 2; \
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

  case OP_IN:     BINARY(_vm_in(lhs, rhs))
  case OP_NOTIN:  BINARY(_vm_notin(lhs, rhs))

  case OP_CONCAT: BINARY(_vm_concat(lhs, rhs))

  case_size_op(OP_CONST, idx,
    {
      Value constant = vm->frame->procedure->code.constants.data[idx];
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
      bool running = execute_instruction(vm);
      push(vm, rhs);
      return running;
    }

  case_size_op(OP_GET, stack_slot,
    {
      push(vm, vm->frame->op_stack[stack_slot]);
      break;
    })
  case_size_op(OP_SET, stack_slot,
    {
      vm->frame->op_stack[stack_slot] = peek(vm);
      break;
    })
  case_size_op(OP_DISCARD_SET, stack_slot,
    {
      vm->frame->op_stack[stack_slot] = pop(vm);
      break;
    })

  case OP_LIST_GET:
    {
      Value idx = pop(vm), list = pop(vm);
      push(vm, _vm_get_elem(list, idx));
      break;
    }
  case OP_LIST_SET:
    {
      Value val = pop(vm), idx = pop(vm), list = pop(vm);
      push(vm, _vm_set_elem(list, idx, val));
      break;
    }

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
      popn(vm, n);
      break;
    })
  case_size_op(OP_RETAIN1_DISCARDN, n,
    {
      Value retained_val = peek(vm);
      popn(vm, n);
      push(vm, retained_val);
      break;
    })

  case_size_op(OP_CALL, argc,
    {
      call_val(vm, peeknth(vm, argc), argc);
      break;
    })
    // Return from a function.
  case OP_RETURN:
    {
      Value return_val = pop(vm);

      CallFrame frame = CallStack_pop(&vm->call_stack);
      popn(vm, (size_t)frame.procedure->arity); // Pop parameters

      if (vm->call_stack.len == 0) {
        // Return from program.
        vm->result = return_val;
        return false;
      }

      push(vm, return_val);
      vm->frame = CallStack_top(&vm->call_stack);
      break;
    }

  default: unreachable();
  }

  return true;

#undef UNARY
#undef BINARY
#undef case_size_op
}

void execute(Varmint *vm, Proc *program)
{
  call(vm, program, 0L);

  bool running;
  do
    running = execute_instruction(vm);
  while (running);

  assert(vm->op_stack.len == 0);
}
