#include "../inc/info.h"
#include "../inc/internal.h"
#include "../inc/util.h"
#include "../inc/val.h"
#include "../inc/vm.h"

static inline void ensure_stack_len(Varmint *vm, size_t len)
{
  if (len >= OP_STACK_MAX)
    runtime_error(vm, "stack overflow");
}

static inline void push(Varmint *vm, Value value)
{
  ensure_stack_len(vm, vm->op_stack.len);
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

static inline Value *top(Varmint *vm)
{
  return OpStack_top(&vm->op_stack);
}

static inline uint8_t read_byte(Varmint *vm)
{
  return *(vm->frame->ip++);
}

static inline uint16_t read_16(Varmint *vm)
{
  vm->frame->ip += 2;
  return uint8_to_16(vm->frame->ip - 2);
}

// Call a procedure.
static void call(Varmint *vm,
    Procedure *procedure,
    Upval **upvalues, size_t upvalue_count)
{
  if (vm->call_stack.len >= CALL_STACK_MAX)
    runtime_error(vm, "maximum call depth exceeded.");

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
      runtime_error(vm, "cannot pass in parameter with no value");

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
    runtime_error(vm, "expect %li parameters to %.*s but got %li",
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
  case V_partial:
    {
      Partial *p = callee.as.partial;
      ensure_stack_len(vm, vm->op_stack.len + p->application_count);

      Value *callee_slot = top(vm) - argc;
      Value *params = callee_slot + 1;

      // Adjust parameter positions.
      for (size_t i = 0; i < argc; i++)
        params[p->application_count + i] = params[i];

      // Place the partially applied parameters
      for (size_t i = 0; i < p->application_count; i++)
        params[i] = p->applied[i];

      vm->op_stack.len += p->application_count;
      *callee_slot = p->callee;

      call_val(vm, p->callee, argc + p->application_count);
      break;
    }
  default:
    runtime_error(vm, "cannot call value of type %s",
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
static inline Upval *capture_local(Varmint *vm, size_t stack_slot)
{
  Value *slot = &vm->frame->op_stack[stack_slot];

  Upval *upval = vm->open_upvalues,
        **prev = &vm->open_upvalues;
  // Find the right gap to insert the upvalue.
  // The upvalues list is sorted to reflect the order of the op stack.
  while (upval != NULL && upval->loc > slot) {
    prev = &(*prev)->next;
    upval = upval->next;
  }

  if (upval != NULL && upval->loc == slot)
    // This upvalue points exactly to the slot we're capturing - reuse it.
    return upval;

  // Create a new upvalue.
  Upval *new_upval = create_gc_obj(vm, V_upval, sizeof(Upval))->as.upval;
  new_upval->loc = slot;

  // Insert into open upvalues list (at the right location).
  new_upval->next = upval;
  *prev = new_upval;
  return new_upval;
}

static inline void validate_assign(Varmint *vm, Typetag value_type)
{
  if (value_type == V_no)
    runtime_error(vm, "invalid assign to expression without value");
}

static inline Value validate_table_key(Varmint *vm, Value key)
{
  if (!value_is_hashable(key.type)) {
    String *s = value_to_string(vm, key).as.string;
    runtime_error(vm, "table key %.*s is not hashable", (int)s->len, s->s);
  }
  return key;
}

static void loop_result(Varmint *vm, Value result)
{
  push(vm, result.type == V_no
      ? Maybe_none() : Maybe_some(vm, result));
}

static void list_comprehend(Varmint *vm, Value value)
{
  if (value.type == V_no)
    runtime_error(vm, "must provide value for list comprehension");

  Value list_val = peek(vm, 0);
  assert(list_val.type == V_list);

  List_push(vm, list_val.as.list, value);
}

static bool for_loop_next(Varmint *vm, Value iterable, size_t counter)
{
  // Value of the loop variable
  Value val = NO_VALUE;

  switch (iterable.type) {
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
    runtime_error(vm, "cannot use %s as iterable in for loop",
        value_type_cstring(iterable.type));
  }

  push(vm, val);
  return true;
}

void call_program(Varmint *vm, Procedure *program)
{
  call(vm, program, NULL, 0);
}

void run_bytecode(Varmint *vm)
{
  // Macros really help with some of the tedium here.

#define UNARY(expr) { \
    Value operand = peek(vm, 0); \
    *top(vm) = (expr); \
    break; \
  }
#define BINARY(expr) { \
    Value rhs = pop(vm); \
    Value lhs = peek(vm, 0); \
    *top(vm) = (expr); \
    break; \
  }

  // Opcode with a variable sized operand (8/16-bit)
#define case_var_op(op_name, ident, stmt) \
  case op_name: \
    { uint8_t ident = read_byte(vm); stmt } \
  case op_name##_16: \
    { uint16_t ident = read_16(vm); stmt }

  for (;;) {
    Opcode instruction = (Opcode)read_byte(vm);

    switch (instruction) {
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

      // Discard a value.
    case OP_POP:
      pop(vm);
      break;
      // Swap stack slots.
    case OP_SWAP:
      {
        Value a = peek(vm, 0), b = peek(vm, 1);
        top(vm)[0] = b;
        top(vm)[-1] = a;
        break;
      }
      // ( a b c -- b a c )
    case OP_SWAP_NEATH:
      {
        Value a = peek(vm, 1), b = peek(vm, 2);
        top(vm)[-1] = b;
        top(vm)[-2] = a;
        break;
      }
      // ( a b -- b a b )
    case OP_SWAP_MOVE_OVER:
      {
        Value a = peek(vm, 0),
              b = peek(vm, 1);
        top(vm)[-1] = a;
        top(vm)[0] = b;
        push(vm, a);
        break;
      }
      // Duplicate two stack slots.
    case OP_DUP_2:
      {
        Value one = peek(vm, 1), two = peek(vm, 0);
        push(vm, one);
        push(vm, two);
        break;
      }
      // (a b c d -- d c b a)
    case_var_op(OP_MIRROR, n,
      {
        Value *bottom_slot = top(vm) - (n - 1);

        for (size_t i = 0, middle = n / 2; i < middle; i++) {
          Value left = bottom_slot[i];
          Value *right = top(vm) - i;

          bottom_slot[i] = *right;
          *right = left;
        }
        break;
      })

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

      // Constructs a table from keys and values.
    case_var_op(OP_BUILD_TABLE, entry_count,
      {
        Value tb = Table_create(vm, entry_count);

        for (size_t i = 0; i < entry_count; i++) {
          Value value = pop(vm);
          Value key = validate_table_key(vm, pop(vm));

          Table_set(vm, tb.as.table, key, value);
        }

        push(vm, tb);
        break;
      })
      // Wrap nested entries
    case_var_op(OP_NESTED_TABLE_ENTRIES, nested_entries,
      {
        for (size_t i = 0; i < nested_entries; i++) {
          Value nested_tb = Table_create(vm, 1);

          Value value = pop(vm);
          Value key = validate_table_key(vm, pop(vm));

          Table_set(vm, nested_tb.as.table, key, value);
          push(vm, nested_tb);
        }
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
        validate_assign(vm, val.type);
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
        validate_assign(vm, val.type);
        *get_upvalue(vm, upval_idx) = val;
        break;
      })

      // Get an element from a collection.
    case OP_GET_ELEM:
      {
        Value idx = pop(vm),
              collection = pop(vm);
        push(vm, _vm_get_elem(vm, collection, idx));
        break;
      }
      // Set an element of a collection.
    case OP_SET_ELEM:
      {
        Value val = pop(vm),
              idx = pop(vm),
              collection = pop(vm);
        validate_assign(vm, val.type);
        push(vm, _vm_set_elem(vm, collection, idx, val));
        break;
      }

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

      // Jump over some code
    case OP_JMP:
      {
        vm->frame->ip += read_16(vm);
        break;
      }
      // Jump when value is False.
    case OP_JMP_WHEN_FALSE:
      {
        size_t jumpable_code = read_16(vm);

        if (value_is_falsey(pop(vm)))
          vm->frame->ip += jumpable_code;
        break;
      }

      // Start an if clause.
      // If lhs is False, jump over the Some()-constructing body and push None
    case OP_IF:
      {
        size_t jumpable_code = read_16(vm);

        if (value_is_falsey(pop(vm))) {
          vm->frame->ip += jumpable_code;
          push(vm, Maybe_none());
        }
        break;
      }
      // Start an else clause.
      // If lhs is Some(), jump over the body and push the unwrapped value.
    case OP_ELSE:
      {
        size_t jumpable_code = read_16(vm);

        Value lhs = pop(vm);
        Maybe *optional = typechecked(vm, lhs, maybe);

        if (optional != NULL) {
          vm->frame->ip += jumpable_code;
          push(vm, optional->raw);
        }
        break;
      }
      // Start an elif clause.
      // If lhs Some(), jump over the if body and push the Some()
    case OP_ELIF:
      {
        size_t jumpable_code = read_16(vm);

        Value lhs = pop(vm);

        if (typechecked(vm, lhs, maybe) != NULL) {
          vm->frame->ip += jumpable_code;
          push(vm, lhs);
        }
        break;
      }

      // Create a list for list comprehension
    case OP_LIST_COMPREHEND:
      push(vm, List_create(vm, 0));
      break;

    case OP_LOOP:
      {
        vm->frame->ip -= read_16(vm);
        break;
      }
      // List comprehension.
      // A loop that creates a list from its cycles' values
    case OP_LOOP_LIST:
      {
        list_comprehend(vm, pop(vm));
        vm->frame->ip -= read_16(vm);
        break;
      }

      // Start a while loop cycle.
    case OP_WHILE:
      {
        size_t jumpable_code = read_16(vm);

        Value cond = pop(vm);
        Value result = pop(vm);

        if (value_is_falsey(cond)) {
          vm->frame->ip += jumpable_code;
          loop_result(vm, result);
        }
        break;
      }
      // Start a while list comprehension cycle.
    case OP_WHILE_LIST:
      {
        size_t jumpable_code = read_16(vm);

        Value cond = pop(vm);

        if (value_is_falsey(cond))
          vm->frame->ip += jumpable_code;
        break;
      }

      // Start a for loop cycle.
    case OP_FOR:
      {
        size_t jumpable_code = read_16(vm);

        Value result = pop(vm);

        size_t counter = (size_t)peek(vm, 0).as.number;
        Value iterable = peek(vm, 1);

        if (!for_loop_next(vm, iterable, counter)) {
          popn(vm, 2);
          vm->frame->ip += jumpable_code;
          loop_result(vm, result);
        }
        break;
      }
      // Start a for list comprehension cycle.
    case OP_FOR_LIST:
      {
        size_t jumpable_code = read_16(vm);

        size_t counter = (size_t)peek(vm, 1).as.number;
        Value iterable = peek(vm, 2);

        if (!for_loop_next(vm, iterable, counter)) {
          Value result_list = pop(vm);
          popn(vm, 2);
          push(vm, result_list);
          vm->frame->ip += jumpable_code;
        }
        break;
      }
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

    case OP_BREAK:
      {
        loop_result(vm, pop(vm));
        vm->frame->ip += read_16(vm);
        break;
      }
    case OP_BREAK_LIST:
      {
        list_comprehend(vm, pop(vm));
        vm->frame->ip += read_16(vm);
        break;
      }

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
              ? capture_local(vm, upval->idx) // stack slot
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

      // Partially apply values to a callable
    case_var_op(OP_PARTIAL, count,
      {
        Value *callee = top(vm) - count;
        Value partial;
        Value *applied;

        if (callee->type == V_partial) {
          // Flatten partial application.
          size_t old_count = callee->as.partial->application_count;
          partial = Partial_create(vm,
              callee->as.partial->callee, old_count + count);

          // Migrate old applied values.
          for (size_t i = 0; i < old_count; i++)
            partial.as.partial->applied[i] = callee->as.partial->applied[i];

          applied = &partial.as.partial->applied[old_count];
        }

        else {
          partial = Partial_create(vm, *callee, count);
          applied = partial.as.partial->applied;
        }

        Value *vals = callee + 1;
        // Move applied values over.
        for (size_t i = 0; i < count; i++)
          applied[i] = vals[i];

        popn(vm, count); // Pop applied parameters
        *top(vm) = partial; // Push partial application.
        break;
      })

      // Call a value
    case_var_op(OP_CALL, argc,
      {
        call_val(vm, peek(vm, argc), argc);
        break;
      })
      // Call a value with a single parameter.
    case OP_CALL_UNARY:
      call_val(vm, peek(vm, 1), 1);
      break;

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
          goto exit;
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
    }
  }

exit:
  return;

#undef UNARY
#undef BINARY
#undef case_var_op
}
