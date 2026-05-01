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
  Upval *new_upval = (Upval *)create_gc_obj(vm, V_upval, sizeof(Upval));
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

// Call a procedure.
static void call(Varmint *vm,
    Procedure *procedure, Value *op_stack,
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

  if (op_stack == NULL)
    // (`argc` + 1) slots for parameters and the fn itself
    frame.op_stack = &vm->op_stack.data[vm->op_stack.len - argc - 1];
  else
    frame.op_stack = op_stack;

  vm->frame = CallStack_push(&vm->call_stack, frame);
}

static void check_fn_argc(Varmint *vm, size_t arity, Str name, size_t argc)
{
  if (name.len == 0)
    name = str_from("function");

  if (arity != argc)
    runtime_error(vm, "expect %li parameters to %.*s but got %li",
        arity, (int)name.len, name.s, argc);
}

// Call a value.
static void call_val(Varmint *vm, Value *callee, size_t argc)
{
  Value *argv = callee + 1;

  switch (callee->type) {
  case V_native:
    {
      ArgList args = {argc, argv};
      Value result = callee->as.native(vm, &args);

      popn(vm, argc); // Pop parameters off the op stack
      *top(vm) = result;
      break;
    }
  case V_cclosure:
    {
      Cclosure *c = callee->as.cclosure;
      ArgList args = {argc, argv};
      Value result = c->fn(vm, &args, c->upvalues);

      popn(vm, argc); // Pop parameters off the op stack
      *top(vm) = result;
      break;
    }
  case V_procedure:
    {
      Procedure *fn = callee->as.procedure;
      check_fn_argc(vm, fn->arity, fn->name, argc);

      call(vm, fn, callee, NULL, 0);
      break;
    }
  case V_closure:
    {
      Closure *c = callee->as.closure;
      Procedure *fn = c->procedure;
      check_fn_argc(vm, fn->arity, fn->name, argc);

      call(vm, fn, callee, c->upvalues, c->upvalue_count);
      break;
    }
  case V_partial:
    {
      Partial *p = callee->as.partial;
      ensure_stack_len(vm, vm->op_stack.len + p->application_count);

      // Adjust parameter positions.
      for (size_t i = 0; i < argc; i++)
        argv[p->application_count + i] = argv[i];

      // Place the partially applied parameters
      for (size_t i = 0; i < p->application_count; i++)
        argv[i] = p->applied[i];

      vm->op_stack.len += p->application_count;
      *callee = p->callee;

      call_val(vm, callee, argc + p->application_count);
      break;
    }
  default:
    runtime_error(vm, "cannot call value of type %s",
        value_type_cstring(callee->type));
  }
}

void call_program(Varmint *vm, Procedure *program)
{
  Value *slot = &vm->op_stack.data[0];

  // Retain old op stack values!
  if (vm->op_stack.len == 0)
    vm->op_stack.len = 1;

  *slot = value_new(program, procedure);

  // Reset calls
  vm->call_stack.len = 0;
  call(vm, program, slot, NULL, 0);

  // Emit builtins.
  if (!vm->builtins_emitted) {
    for (size_t i = 0; i < vm->builtins.len; i++)
      push(vm, vm->builtins.data[i].value);

    vm->builtins_emitted = true;
  }
}

void run_bytecode(Varmint *vm)
{
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
        push(vm, _vm_get_elem(vm, collection, idx, false));
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

      // Try to get an element from a collection, wrap result in a Maybe
    case OP_MAYBE_GET_ELEM:
      {
        Value idx = pop(vm),
              collection = pop(vm);
        push(vm, _vm_get_elem(vm, collection, idx, true));
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
      // Get rid of stack slots used by function
    case OP_END_SLOTS:
      {
        Value result = peek(vm, 0);

        vm->op_stack.len =
          (size_t)(vm->frame->op_stack - vm->op_stack.data)
            + vm->frame->procedure->arity + 1;

        if (vm->call_stack.len == 1)
          // On program level, take builtin slots into account
          vm->op_stack.len += vm->builtins.len;

        push(vm, result);
        break;
      }

      // Jump over some code
    case OP_JMP:
      {
        vm->frame->ip += read_16(vm);
        break;
      }
      // Jump when value is False.
    case OP_JMP_WHEN_FALSE:
      {
        size_t jump = read_16(vm);

        if (value_is_falsey(pop(vm)))
          vm->frame->ip += jump;
        break;
      }

      // Start an if clause.
      // If lhs is False, jump over the Some()-constructing body and push None
    case OP_IF:
      {
        size_t jump = read_16(vm);

        if (value_is_falsey(pop(vm))) {
          vm->frame->ip += jump;
          push(vm, Maybe_none());
        }
        break;
      }
      // Start an else clause.
      // If lhs is Some(), jump over the body and push the unwrapped value.
    case OP_ELSE:
      {
        size_t jump = read_16(vm);

        Value lhs = pop(vm);
        Maybe *optional = typechecked(vm, lhs, maybe);

        if (optional != NULL) {
          vm->frame->ip += jump;
          push(vm, optional->raw);
        }
        break;
      }
      // Start an elif clause.
      // If lhs Some(), jump over the if body and push the Some()
    case OP_ELIF:
      {
        size_t jump = read_16(vm);

        Value lhs = pop(vm);

        if (typechecked(vm, lhs, maybe) != NULL) {
          vm->frame->ip += jump;
          push(vm, lhs);
        }
        break;
      }

      // Initialize loop result slot
    case OP_INIT_LOOP:
      {
        Value initial_result = NO_VALUE;
        initial_result.as.metadata.loop_has_run = false;

        push(vm, initial_result);
        break;
      }
      // Update result slot and jump back to the top of the loop code
    case OP_LOOP:
      {
        size_t jump = read_16(vm);
        vm->frame->ip -= jump;

        Value result = pop(vm);
        if (result.type == V_no)
          result.as.metadata.loop_has_run = true;

        *top(vm) = result;
        break;
      }
      // Initialize loop variable with next value from iterable.
    case OP_FOR:
      {
        Value *iterable = &top(vm)[-1];

        if (value_is_callable(iterable->type)) {
          push(vm, *iterable);
          call_val(vm, iterable, 0);
        }
        else runtime_error(vm, "iterable %s is not a callable value",
            value_type_cstring(iterable->type));

        break;
      }
      // Break from the loop if the iterable is finished.
    case OP_FOR_JMP:
      {
        size_t jump = read_16(vm);
        Value next_val = peek(vm, 0);

        if (next_val.type != V_maybe)
          runtime_error(vm, "expect maybe return type for iterable, got %s",
              value_type_cstring(next_val.type));

        else if (next_val.as.maybe == NULL) {
          // Iterable returned None
          // -> Discard the slots of the loop variable and the iterable.
          pop(vm);
          Value loop_result = pop(vm);
          *top(vm) = loop_result;

          // Break from loop.
          vm->frame->ip += jump;
        }
        else
          // Iterable returned Some.
          *top(vm) = next_val.as.maybe->raw;

        break;
      }
      // Discard iterable.
    case OP_FOR_DISCARD:
      {
        Value result_val = pop(vm);
        *top(vm) = result_val;
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
        call_val(vm, top(vm) - argc, argc);
        break;
      })
      // Call a value with a single parameter.
    case OP_CALL_UNARY:
      call_val(vm, top(vm) - 1, 1);
      break;

      // Return from a function.
    case OP_RETURN:
      {
        Value return_val = pop(vm);
        CallFrame frame = CallStack_pop(&vm->call_stack);

        bool return_from_program = vm->call_stack.len == 0;

        // Discard builtins.
        if (return_from_program) {
          assert(vm->builtins_emitted);

          popn(vm, vm->builtins.len);
          vm->builtins_emitted = false;
        }

        // Pop function parameters
        popn(vm, frame.procedure->arity);
        // Pop the function itself off the stack.
        pop(vm);

#ifdef VARMINT_DEBUG
        // Ensure a balanced stack after the call!
        if (&vm->op_stack.data[vm->op_stack.len] != frame.op_stack) {
          print_op_stack(vm);
          assert(false);
        }
#endif

        if (return_from_program) {
          vm->result = return_val;
          return;
        }

        // Push return value
        push(vm, return_val);

        vm->frame = CallStack_top(&vm->call_stack);
        break;
      }

      // Suspend program execution in order to continue at a later time.
    case OP_SUSPEND:
      vm->result =
        vm->status == VM_A_OK ? peek(vm, 0) : NO_VALUE;
      return;

      // Halt erroneous execution.
    case OP_HALT:
      return;

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

#undef UNARY
#undef BINARY
#undef case_var_op
}
