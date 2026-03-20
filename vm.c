#include "info.h"
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
static void call(Varmint *vm,
    Procedure *procedure,
    Upval **upvalues, size_t upvalue_count)
{
  if (vm->call_stack.len >= CALL_STACK_MAX)
    runtime_error(vm, "maximum call depth exceeded.\n");

  // Create new frame for procedure call.
  CallFrame frame;
  frame.procedure = procedure;
  frame.ip = procedure->code.instructions.data;

  frame.upvalues = upvalues;
  frame.upvalue_count = upvalue_count;

  size_t argc = procedure->arity;

  if (vm->op_stack.len > 0)
    // (`argc` + 1) slots for parameters and the fn itself
    frame.op_stack = &vm->op_stack.data[vm->op_stack.len - argc - 1];

  else {
    // Begin program.
    frame.op_stack = &vm->op_stack.data[0];
    OpStack_push(&vm->op_stack, value_new(procedure, procedure));
  }

  vm->frame = CallStack_push(&vm->call_stack, frame);
}

// Call a native function.
static void call_native(Varmint *vm, Native *native)
{
  Value *params = allocate(NULL, (size_t)native->arity * sizeof(Value));
  // Get parameters
  for (size_t i = 1; i <= native->arity; i++) {
    Value param = pop(vm);
    if (param.type == V_no)
      runtime_error(vm, "cannot pass in parameter with no value\n");

    params[native->arity - i] = param;
  }

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
  switch (callee.type) {
  case V_native:
    {
      Native *fn = &vm->natives.data[callee.as.native];
      check_fn_argc(vm, fn->arity, fn->name, argc);

      call_native(vm, fn);
      break;
    }
  case V_procedure:
    {
      Procedure *fn = callee.as.procedure;
      check_fn_argc(vm, fn->arity, fn->name, argc);

      call(vm, fn, NULL, 0);
      break;
    }
  case V_closure:
    {
      Closure *c = callee.as.closure;
      Procedure *fn = c->procedure;
      check_fn_argc(vm, fn->arity, fn->name, argc);

      call(vm, fn, c->upvalues, c->upvalue_count);
      break;
    }
  default:
    runtime_error(vm, "cannot call value of type %s\n",
        value_type_cstring(callee.type));
  }
}

static inline Value *get_stack_slot(Varmint *vm, size_t stack_slot)
{
  Value *slot = &vm->frame->op_stack[stack_slot];
  // Ensure valid index
  assert(slot < &vm->op_stack.data[vm->op_stack.len]);
  return slot;
}

static inline Value *get_upvalue(Varmint *vm, size_t upval_idx)
{
  // Ensure valid index
  assert(upval_idx < vm->frame->upvalue_count);
  return vm->frame->upvalues[upval_idx]->loc;
}

// Capture a local stack slot.
static inline Upval *capture_upvalue(Varmint *vm, size_t stack_slot)
{
  Value *slot = &vm->frame->op_stack[stack_slot];

  Upval *upval = vm->open_upvalues,
        **prev = &vm->open_upvalues;
  // Find the right gap to insert the upvalue.
  while (upval != NULL && upval->loc > slot) {
    *prev = upval;
    upval = upval->next;
  }

  if (upval != NULL && upval->loc == slot)
    // This upvalue points exactly to the slot we're capturing - reuse it.
    return upval;

  // Create a new upvalue.
  Upval *new_upval = create_gc_obj(vm, V_upval, sizeof(Upval))->as.upval;
  new_upval->loc = &vm->frame->op_stack[stack_slot];

  // Insert into open upvalues list (at the right location).
  new_upval->next = upval;
  *prev = new_upval;
  return new_upval;
}

static inline void validate_assign(Varmint *vm, Value val)
{
  if (val.type == V_no)
    runtime_error(vm, "invalid assign to expression without value\n");
}

static void loop_result(Varmint *vm, Value result)
{
  push(vm, result.type == V_no
      ? Maybe_none() : Maybe_some(vm, result));
}

static void list_comprehend(Varmint *vm, Value value)
{
  if (value.type == V_no)
    runtime_error(vm, "must provide value for list comprehension\n");

  Value list_val = peek(vm, 0);
  assert(list_val.type == V_list);

  List_push(vm, list_val.as.list, value);
}

static bool for_loop_next(Varmint *vm, Value iterable, size_t counter)
{
  // Value of the loop variable
  Value val = NO_VALUE;

  switch (iterable.type) {
  case V_range:
    {
      Range *range = iterable.as.range;
      float64_t point = range->start + (float64_t)counter;
      bool in_range =
        range->inclusive
          ? point <= range->end : point < range->end;

      if (in_range)
        val = value_new(point, number);
      else return false;
      break;
    }
  case V_list:
    if (counter < iterable.as.list->len)
      val = iterable.as.list->data[counter];
    else return false;
    break;
  case V_string:
    if (counter < iterable.as.string->len)
      val = String_create(vm, &iterable.as.string->s[counter], 1);
    else return false;
    break;
  default:
    runtime_error(vm, "cannot use %s as iterable in for loop\n",
        value_type_cstring(iterable.type));
  }

  push(vm, val);
  return true;
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

#define case_op(op_name, decl, expr, ip_increment, stmt) \
  case op_name: \
    { decl = expr; vm->frame->ip += ip_increment; stmt; }

  // Opcode with a 16-bit operand
#define case_16_op(op_name, ident, stmt) \
    case_op(op_name, uint16_t ident, uint8_to_16(vm->frame->ip), 2, stmt)

  // Opcode with a variable sized operand (8/16-bit)
#define case_var_op(op_name, ident, stmt) \
    case_op(op_name,     uint8_t ident,  *vm->frame->ip,             1, stmt) \
    case_op(op_name##16, uint16_t ident, uint8_to_16(vm->frame->ip), 2, stmt)

  switch ((int)instruction) {
  case OP_NOT:    UNARY(value_new((int)value_is_falsey(operand), boolean))
  case OP_NEGATE: UNARY(_vm_negate(vm, operand))

  case OP_FACTORIAL:  UNARY(_vm_factorial(vm, operand))
  case OP_PERCENTAGE: UNARY(_vm_percentage(vm, operand))

    // Unwrap optional, error if None.
  case OP_UNWRAPPED:
    {
      Value unwrappee = pop(vm);
      Maybe *optional = typechecked(vm, unwrappee, maybe);

      if (optional == NULL)
        runtime_error(vm, "unwrap of None\n");

      push(vm, optional->raw);
      break;
    }

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

  case OP_RANGE:    BINARY(_vm_range(vm, lhs, rhs, false))
  case OP_RANGE_IN: BINARY(_vm_range(vm, lhs, rhs, true))

  case OP_CONCAT: BINARY(_vm_concat(vm, lhs, rhs))

    // Load a constant value.
  case_var_op(OP_CONST, idx,
    {
      Value constant = vm->frame->procedure->code.constants.data[idx];
      push(vm, constant);
      break;
    })

  case OP_ZERO:
    {
      const Value zero = value_new(0.0, number);
      push(vm, zero);
      break;
    }
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

    // Duplicate two stack slots.
  case OP_DUP_2:
    {
      Value one = peek(vm, 1), two = peek(vm, 0);
      push(vm, one);
      push(vm, two);
      break;
    }

    // Weaves a list.
  case_var_op(OP_BUILD_LIST, len,
    {
      Value list_val = List_create(vm, len);
      list_val.as.list->len = len;

      for (int i = len - 1; i >= 0; i--)
        list_val.as.list->data[i] = pop(vm);

      push(vm, list_val);
      break;
    })

    // Stitches together the metastrings emitted by the compiler.
  case_var_op(OP_BUILD_STR, metas,
    {
      Value string_val = value_to_string(vm, pop(vm));

      for (size_t i = 1; i < metas; i++) {
        Value meta = value_to_string(vm, pop(vm));
        string_val = String_concat(vm, &meta, &string_val);
      }

      push(vm, string_val);
      break;
    })

    // Create optional values
  case OP_MAKE_SOME:
    push(vm, Maybe_some(vm, pop(vm)));
    break;
  case OP_MAKE_NONE:
    push(vm, Maybe_none());
    break;

    // Get a value on the stack.
  case_var_op(OP_GET, stack_slot,
    {
      push(vm, *get_stack_slot(vm, stack_slot));
      break;
    })
    // Set a value on the stack.
  case_var_op(OP_SET, stack_slot,
    {
      Value val = peek(vm, 0);
      validate_assign(vm, val);
      *get_stack_slot(vm, stack_slot) = val;
      break;
    })

    // Get an upvalue
  case_var_op(OP_GET_UPVALUE, upval_idx,
    {
      push(vm, *get_upvalue(vm, upval_idx));
      break;
    })
    // Set an upvalue
  case_var_op(OP_SET_UPVALUE, upval_idx,
    {
      Value val = peek(vm, 0);
      validate_assign(vm, val);
      *get_upvalue(vm, upval_idx) = val;
      break;
    })

    // Get an element from a collection.
  case OP_INDEXED_GET:
    {
      Value idx = pop(vm),
            collection = pop(vm);
      push(vm, _vm_get_elem(vm, collection, idx));
      break;
    }
    // Set an element of a collection.
  case OP_INDEXED_SET:
    {
      Value val = pop(vm),
            idx = pop(vm),
            collection = pop(vm);
      validate_assign(vm, val);
      push(vm, _vm_set_elem(vm, collection, idx, val));
      break;
    }

    // Discard a value.
  case OP_POP:
    pop(vm);
    break;

    // Reserve a slot on the stack.
  case OP_RESERVE_SLOT:
    push(vm, NO_VALUE);
    break;
    // End code block
  case_var_op(OP_END_BLOCK, n,
    {
      Value block_val = peek(vm, 0);
      popn(vm, n);
      push(vm, block_val);
      break;
    })
    // End code block with no value.
  case_var_op(OP_END_EMPTY_BLOCK, n,
    {
      popn(vm, n);
      push(vm, NO_VALUE);
      break;
    })

    // Jump over some code
  case_16_op(OP_JMP, jumpable_code,
    {
      vm->frame->ip += jumpable_code;
      break;
    })
    // Jump when value is False.
  case_16_op(OP_JMP_WHEN_FALSE, jumpable_code,
    {
      if (value_is_falsey(pop(vm)))
        vm->frame->ip += jumpable_code;
      break;
    })

    // Start an if clause.
    // If lhs is False, jump over the Some()-constructing body and push None
  case_16_op(OP_IF, jumpable_code,
    {
      if (value_is_falsey(pop(vm))) {
        vm->frame->ip += jumpable_code;
        push(vm, Maybe_none());
      }
      break;
    })
    // Start an else clause.
    // If lhs is Some(), jump over the body and push the unwrapped value.
  case_16_op(OP_ELSE, jumpable_code,
    {
      Value lhs = pop(vm);
      Maybe *optional = typechecked(vm, lhs, maybe);
      if (optional != NULL) {
        vm->frame->ip += jumpable_code;
        push(vm, optional->raw);
      }
      break;
    })
    // Start an elif clause.
    // If lhs Some(), jump over the if body and push the Some()
  case_16_op(OP_ELIF, jumpable_code,
    {
      Value lhs = pop(vm);
      if (typechecked(vm, lhs, maybe) != NULL) {
        vm->frame->ip += jumpable_code;
        push(vm, lhs);
      }
      break;
    })

    // Create a list for list comprehension
  case OP_LIST_COMPREHEND:
    push(vm, List_create(vm, 0));
    break;

  case_16_op(OP_LOOP, loopable_code,
    {
      vm->frame->ip -= loopable_code;
      break;
    })
    // List comprehension.
    // A loop that creates a list from its cycles' values
  case_16_op(OP_LOOP_LIST, loopable_code,
    {
      list_comprehend(vm, pop(vm));
      vm->frame->ip -= loopable_code;
      break;
    })

    // Start a while loop cycle.
  case_16_op(OP_WHILE, jumpable_code,
    {
      Value cond = pop(vm);
      Value result = pop(vm);

      if (value_is_falsey(cond)) {
        vm->frame->ip += jumpable_code;
        loop_result(vm, result);
      }
      break;
    })
    // Start a while list comprehension cycle.
  case_16_op(OP_WHILE_LIST, jumpable_code,
    {
      Value cond = pop(vm);

      if (value_is_falsey(cond))
        vm->frame->ip += jumpable_code;
      break;
    })

    // Start a for loop cycle.
  case_16_op(OP_FOR, jumpable_code,
    {
      Value result = pop(vm);

      size_t counter = (size_t)peek(vm, 0).as.number;
      Value iterable = peek(vm, 1);

      if (!for_loop_next(vm, iterable, counter)) {
        popn(vm, 2);
        vm->frame->ip += jumpable_code;
        loop_result(vm, result);
      }
      break;
    })
    // Start a for list comprehension cycle.
  case_16_op(OP_FOR_LIST, jumpable_code,
    {
      size_t counter = (size_t)peek(vm, 1).as.number;
      Value iterable = peek(vm, 2);

      if (!for_loop_next(vm, iterable, counter)) {
        Value result_list = pop(vm);
        popn(vm, 2);
        push(vm, result_list);
        vm->frame->ip += jumpable_code;
      }
      break;
    })
    // Discard loop variable slot & increment counter
  case_var_op(OP_FOR_INCREMENT, stack_slot,
    {
      Value result = pop(vm);
      pop(vm); // Loop variable

      Value *counter = get_stack_slot(vm, stack_slot);
      assert(counter->type == V_number);
      counter->as.number++;

      push(vm, result);
      break;
    })

  case_16_op(OP_BREAK, jumpable_code,
    {
      loop_result(vm, pop(vm));
      vm->frame->ip += jumpable_code;
      break;
    })
  case_16_op(OP_BREAK_LIST, jumpable_code,
    {
      list_comprehend(vm, pop(vm));
      vm->frame->ip += jumpable_code;
      break;
    })

  case OP_DISCARD_FOR:
    {
      Value result = pop(vm);

      // Discard the counter and iterable
      popn(vm, 2);

      push(vm, result);
      break;
    }
  case OP_DISCARD_FOR_LIST:
    {
      Value result = pop(vm);
      Value list = pop(vm);

      // Discard the counter and iterable
      popn(vm, 2);

      push(vm, list);
      push(vm, result);
      break;
    }

    // Wrap a function into a closure.
  case OP_CLOSURE:
    {
      Procedure *proc = pop(vm).as.procedure;
      Value c = Closure_create(vm, proc);

      // Capture and initialize upvalues from enclosing function.
      for (size_t i = 0; i < proc->closure_desc.len; i++) {
        UpvalDesc *upval = &proc->closure_desc.data[i];

        c.as.closure->upvalues[i] =
          upval->captures_local
            ? capture_upvalue(vm, upval->idx) // stack slot
            : vm->frame->upvalues[upval->idx]; // captured upvalue
      }

      push(vm, c);
      break;
    }
    // Hoist an upvalue to the heap on scope end.
  case OP_HOIST_UPVALUE:
    {
      assert(vm->open_upvalues != NULL);

      Upval *upval = vm->open_upvalues;
      vm->open_upvalues = upval->next;

      upval->hoisted = *upval->loc;
      upval->loc = &upval->hoisted;
      break;
    }

    // Call a value
  case_var_op(OP_CALL, argc,
    {
      call_val(vm, peek(vm, argc), argc);
      break;
    })
    // Return from a function.
  case OP_RETURN:
    {
      Value return_val = pop(vm);
      CallFrame frame = CallStack_pop(&vm->call_stack);

      // Pop function parameters
      popn(vm, (size_t)frame.procedure->arity);
      // Pop the function itself off the stack.
      pop(vm);

      // Ensure a balanced stack after the call!
      assert(&vm->op_stack.data[vm->op_stack.len] == frame.op_stack);

      if (vm->call_stack.len == 0) {
        // Return from program.
        vm->result = return_val;
        return false;
      }

      // Push return value
      push(vm, return_val);

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
#undef case_op
#undef case_16_op
#undef case_var_op
}

void execute(Varmint *vm, Procedure *program)
{
  call(vm, program, NULL, 0);

  bool running;
  do
    running = execute_instruction(vm);
  while (running);
}
