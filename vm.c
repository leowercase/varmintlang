#include "internal.h"
#include "util.h"
#include "vm.h"

#define T Value
#define TYPE_NAME ProgramStack
#include "dyn_array.h"

#include <math.h>

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

static inline bool execute_instruction(VM *vm, PCode *code)
{
  Opcode instruction = *(vm->ip++);

#define UNARY_FN(fn) { \
  Value operand = pop(vm); \
  push(vm, (fn)); \
  break; \
}
#define UNARY_OP(op) UNARY_FN(op operand)

#define BINARY_FN(fn) { \
  Value rhs = pop(vm); \
  Value lhs = pop(vm); \
  push(vm, (fn)); \
  break; \
}
#define BINARY_OP(op) BINARY_FN(lhs op rhs)

  switch (instruction) {
  case OP_NONE:
    abort(); // Unreachable
  case OP_CONST:
    {
      Value constant = code->constants.data[*vm->ip];
      push(vm, constant);
      vm->ip++;
      break;
    }
  case OP_CONST16:
    {
      Value constant = code->constants.data[uint8_to_16(vm->ip)];
      push(vm, constant);
      vm->ip += 2;
      break;
    }
  case OP_NOT:    UNARY_OP(!)
  case OP_NEGATE: UNARY_OP(-)

  case OP_FACTORIAL:  UNARY_FN(factorial(operand))
  case OP_PERCENTAGE: UNARY_FN(operand * 0.01)

  case OP_ADD: BINARY_OP(+)
  case OP_SUB: BINARY_OP(-)
  case OP_MUL: BINARY_OP(*)
  case OP_DIV: BINARY_OP(/)

  case OP_POW:    BINARY_FN(pow(lhs, rhs))
  case OP_MODULO: BINARY_FN(fmod(lhs, rhs))

  case OP_AND: BINARY_OP(&&)
  case OP_OR:  BINARY_OP(||)
  case OP_I9N: BINARY_FN(implies(lhs, rhs))

  case OP_EQ:  BINARY_FN(equals(lhs, rhs))
  case OP_NEQ: BINARY_FN(!equals(lhs, rhs))
  case OP_LT:  BINARY_OP(<)
  case OP_GT:  BINARY_OP(>)
  case OP_LEQ: BINARY_OP(<=)
  case OP_GEQ: BINARY_OP(>=)

  case OP_CHAIN_BINOP:
    {
      // The good ol' switcheroo.
      Value rhs = peek(vm);
      bool running = execute_instruction(vm, code);
      push(vm, rhs);
      return running;
    }

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
