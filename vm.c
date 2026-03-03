#include "error.h"
#include "internal.h"
#include "util.h"
#include "val.h"
#include "vm.h"

static inline void push(Varmint *vm, Value value)
{
  if (vm->op_stack.len >= OP_STACK_MAX)
    runtime_error(vm, "stack overflow\n");

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

static inline Value peek(Varmint *vm, size_t idx)
{
  return *(OpStack_top(&vm->op_stack) - idx);
}

// Call a procedure.
static void call(Varmint *vm, Procedure *procedure, size_t argc)
{
  if (vm->call_stack.len >= CALL_STACK_MAX)
    runtime_error(vm, "maximum call depth exceeded.\n");

  // Create new frame for procedure call.
  CallFrame frame;
  frame.procedure = procedure;
  frame.ip = procedure->code.instructions.data;

  if (vm->op_stack.len > 0)
    frame.op_stack = &vm->op_stack.data[vm->op_stack.len - argc];
  else                                          // `argc` slots for parameters
    frame.op_stack = &vm->op_stack.data[0];

  vm->frame = CallStack_push(&vm->call_stack, frame);
}

// Call a native function.
static void call_native(Varmint *vm, Native *native)
{
  Value *params = allocate(NULL, (size_t)native->arity * sizeof(Value));
  // Get parameters
  for (size_t i = 1; i <= native->arity; i++)
    params[native->arity - i] = pop(vm);

  // Call native function.
  Value result = native->fn(vm, params);

  free(params);
  pop(vm); // Pop native function value off the op stack
  push(vm, result);
}

static void check_fn_argc(Varmint *vm, size_t arity, Str name, size_t argc)
{
  if (name.len == 0)
    name = str_from("function");

  if (arity != argc)
    runtime_error(vm, "expect %li parameters to %.*s but got %li\n",
        arity, (int)name.len, name.s, argc);
}

static void call_val(Varmint *vm, Value callee, size_t argc)
{
  if (callee.type == V_procedure) {
    Procedure *fn = callee.as.procedure;
    check_fn_argc(vm, fn->arity, fn->name, argc);

    call(vm, fn, argc);
  }

  else if (callee.type == V_native) {
    Native *fn = &vm->natives.data[callee.as.native];
    check_fn_argc(vm, fn->arity, fn->name, argc);

    call_native(vm, fn);
  }

  else runtime_error(vm, "cannot call value of type %s\n",
      value_type_cstring(callee.type));
}

static inline bool execute_instruction(Varmint *restrict vm)
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

  // Opcode with a variable sized operand (8/16-bit)
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
  case OP_NEGATE: UNARY(_vm_negate(vm, operand))

  case OP_FACTORIAL:  UNARY(_vm_factorial(vm, operand))
  case OP_PERCENTAGE: UNARY(_vm_percentage(vm, operand))

  case OP_ADD: BINARY(_vm_add(vm, lhs, rhs))
  case OP_SUB: BINARY(_vm_subtract(vm, lhs, rhs))
  case OP_MUL: BINARY(_vm_multiply(vm, lhs, rhs))
  case OP_DIV: BINARY(_vm_divide(vm, lhs, rhs))

  case OP_POW:    BINARY(_vm_pow(vm, lhs, rhs))
  case OP_MODULO: BINARY(_vm_modulo(vm, lhs, rhs))

  case OP_AND: BINARY(_vm_and(vm, lhs, rhs))
  case OP_OR:  BINARY(_vm_or(vm, lhs, rhs))
  case OP_I9N: BINARY(_vm_implies(vm, lhs, rhs))

  case OP_EQ:  BINARY(value_new((int)values_eq(lhs, rhs), boolean))
  case OP_NEQ: BINARY(value_new((int)!values_eq(lhs, rhs), boolean))

  case OP_LT:  BINARY(_vm_less_than(vm, lhs, rhs))
  case OP_GT:  BINARY(_vm_greater_than(vm, lhs, rhs))
  case OP_LEQ: BINARY(_vm_less_than_or_eq(vm, lhs, rhs))
  case OP_GEQ: BINARY(_vm_greater_than_or_eq(vm, lhs, rhs))

  case OP_IN:     BINARY(_vm_in(vm, lhs, rhs))
  case OP_NOTIN:  BINARY(_vm_notin(vm, lhs, rhs))

  case OP_CONCAT: BINARY(_vm_concat(vm, lhs, rhs))

    // Load a constant value.
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

    // Chains the right hand side of an op to be the left hand of another.
    // The good ol' switcheroo.
  case OP_CHAIN_BINOP:
    {
      Value rhs = peek(vm, 0);
      bool running = execute_instruction(vm);
      push(vm, rhs);
      return running;
    }

    // Weaves a list.
  case_size_op(OP_BUILD_LIST, len,
    {
      Value *list_val = List_create(vm, len);
      list_val->as.list->len = len;

      for (int i = len - 1; i >= 0; i--)
        list_val->as.list->data[i] = pop(vm);

      push(vm, *list_val);
      break;
    })

    // Stitches together the metastrings emitted by the compiler.
  case_size_op(OP_BUILD_STR, metas,
    {
      Value *string_val = value_to_string(vm, pop(vm));

      for (size_t i = 1; i < metas; i++)
        string_val = String_concat(vm,
            value_to_string(vm, pop(vm)), string_val);

      push(vm, *string_val);
      break;
    })

    // Create optional values
  case OP_MAKE_SOME:
    push(vm, *Maybe_some(vm, pop(vm)));
    break;
  case OP_MAKE_NONE:
    push(vm, *Maybe_none(vm));
    break;
    // Unwrap a Some() value
  case OP_UNWRAP_MAYBE:
    {
      Maybe *optional = pop(vm).as.maybe;
      assert(optional->is_some);

      push(vm, optional->raw);
      break;
    }

    // Get a value on the stack.
  case_size_op(OP_GET, stack_slot,
    {
      push(vm, vm->frame->op_stack[stack_slot]);
      break;
    })
    // Set a value on the stack.
  case_size_op(OP_SET, stack_slot,
    {
      Value val = peek(vm, 0);

      if (val.type == V_no)
        runtime_error(vm, "invalid assign to expression without value\n");

      vm->frame->op_stack[stack_slot] = val;
      break;
    })

    // Get an element from a collection.
  case OP_INDEXED_GET:
    {
      Value idx = pop(vm),
            list = pop(vm);
      push(vm, _vm_get_elem(vm, list, idx));
      break;
    }
    // Set an element of a collection.
  case OP_INDEXED_SET:
    {
      Value val = pop(vm),
            idx = pop(vm),
            list = pop(vm);

      if (val.type == V_no)
        runtime_error(vm, "invalid list assign to expression without value\n");

      push(vm, _vm_set_elem(vm, list, idx, val));
      break;
    }

    // Reserve a slot on the stack.
  case OP_RESERVE_SLOT:
    push(vm, NO_VALUE);
    break;
    // End code block
  case_size_op(OP_END_BLOCK, n,
    {
      Value block_val = peek(vm, 0);
      popn(vm, n);
      push(vm, block_val);
      break;
    })
    // End code block with no value.
  case_size_op(OP_END_EMPTY_BLOCK, n,
    {
      popn(vm, n);
      push(vm, NO_VALUE);
      break;
    })

    // Jump over some code
  case OP_JMP:
    {
      uint16_t jumpable_code = uint8_to_16(vm->frame->ip);
      vm->frame->ip += 2;
      vm->frame->ip += jumpable_code;
      break;
    }

    // Start an if clause.
    // If lhs is False, jump over the Some()-constructing body and push None
  case OP_IF:
    {
      uint16_t jumpable_code = uint8_to_16(vm->frame->ip);
      vm->frame->ip += 2;

      if (value_is_falsey(pop(vm))) {
        vm->frame->ip += jumpable_code;
        push(vm, *Maybe_none(vm));
      }
      break;
    }
    // Start an else clause.
    // If lhs is Some(), jump over the body and push the unwrapped value.
  case OP_ELSE:
    {
      uint16_t jumpable_code = uint8_to_16(vm->frame->ip);
      vm->frame->ip += 2;

      Value lhs = pop(vm);
      Maybe *optional = typechecked(vm, lhs, maybe);
      if (optional->is_some) {
        vm->frame->ip += jumpable_code;
        push(vm, optional->raw);
      }
      break;
    }
    // Start an elif clause.
    // If lhs Some(), jump over the if body and push the Some()
  case OP_ELIF:
    {
      uint16_t jumpable_code = uint8_to_16(vm->frame->ip);
      vm->frame->ip += 2;

      Value lhs = pop(vm);
      if (typechecked(vm, lhs, maybe)->is_some) {
        vm->frame->ip += jumpable_code;
        push(vm, lhs);
      }
      break;
    }
    // Start an if..elif..else chain -> don't construct Some()/None
  case OP_IF_ELSE_CHAIN:
    {
      uint16_t jumpable_code = uint8_to_16(vm->frame->ip);
      vm->frame->ip += 2;

      if (value_is_falsey(pop(vm)))
        vm->frame->ip += jumpable_code;
      break;
    }

    // Call a value
  case_size_op(OP_CALL, argc,
    {
      call_val(vm, peek(vm, argc), argc);
      break;
    })
    // Return from a function.
  case OP_RETURN:
    {
      Value return_val = pop(vm);
      CallFrame frame = CallStack_pop(&vm->call_stack);

      if (vm->call_stack.len == 0) {
        // Return from program.
        vm->result = return_val;
        return false;
      }

      // Pop function parameters
      popn(vm, (size_t)frame.procedure->arity);
      // Pop the function itself off the stack.
      pop(vm);
      // Push return value
      push(vm, return_val);

      // Ensure a balanced stack after the call!
      assert(vm->op_stack.data + vm->op_stack.len == frame.op_stack);

      vm->frame = CallStack_top(&vm->call_stack);
      break;
    }

    // Collect garbage.
    // This instruction is only ever encountered by the VM when the garbage
    // collector manually sets ip pointing to it.
  case OP_GC:
    vm->frame->ip--; // NB: Backstep to the instruction, so GC knows where at.
    gcollect(vm);
    vm->frame->ip = vm->gc_resume_ip; // Pick up where we left off.
    break;

  default:
    unreachable();
  }

  return true;

#undef UNARY
#undef BINARY
#undef case_size_op
}

void execute(Varmint *vm, Procedure *program)
{
  OpStack_push(&vm->op_stack, value_new(program, procedure));
  call(vm, program, 0);

  bool running;
  do
    running = execute_instruction(vm);
  while (running);
}
