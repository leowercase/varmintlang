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

static void push(VM *vm, Value value)
{
  Stack_push(&vm->stack, value);
}

static Value pop(VM *vm)
{
  Stack_pop(&vm->stack);
}

void vm_run(VM *vm, PCode *code)
{
  vm->ip = code->instruc.data;

  for (;;) {
    Opcode instruction = *(vm->ip++);

    switch (instruction) {
    case OP_NONE:
      abort(); // Unreachable
    case OP_CONSTANT:
      {
        Value constant = code->constants.data[*vm->ip];
        push(vm, constant);
        vm->ip++;
        break;
      }
    case OP_CONSTANT_16:
      {
        Value constant = code->constants.data[uint8_to_16(vm->ip)];
        push(vm, constant);
        vm->ip += 2;
        break;
      }
    case OP_NOT:
    case OP_NEGATE:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_POW:
    case OP_MODULO:
    case OP_PERCENTAGE:
    case OP_FACTORIAL:
    case OP_EQ:
    case OP_NEQ:
    case OP_LT:
    case OP_GT:
    case OP_LEQ:
    case OP_GEQ:
    case OP_AND:
    case OP_OR:
    case OP_I9N:
    case OP_CMP_RHS:
    }
  }
}

void vm_free(VM *vm)
{
  free(vm->stack.data);
}
