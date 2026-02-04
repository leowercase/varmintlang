#include "astree.h"
#include "proc.h"
#include "varmint.h"
#include <stdio.h>

// Local variable that resides on the stack
typedef struct {
  StrSlice name;
  int depth;
  bool initialized;
  size_t stack_slot;
} Local;

typedef DYN_ARRAY_STRUCT(Local) Locals;
#define T Local
#define ARR Locals
#include "generic/dyn_array.inc"

// Scope of a function
typedef struct FnScope {
  Locals locals;
  int block_depth; // { { ... } }
  struct FnScope *enclosing_scope;
  Proc *fn;
} FnScope;

static Local *resolve_local(FnScope *scope, Str name)
{
  for (size_t i = 0; i < scope->locals.len; i++) {
    Local *local = &scope->locals.data[i];
    if (strs_eq(local->name, name))
      return local;
  }
  return NULL;
}

static void create_local(FnScope *scope, Str name)
{
  Local local;
  local.name = name;
  local.depth = scope->block_depth;
  local.initialized = false;
  local.stack_slot = scope->locals.len;

  Locals_push(&scope->locals, local);
}

static void compile_branch(FnScope *scope, Tnode *branch);

static void operator(FnScope *scope, Tnode *op, int arity)
{
  for (int i = 0; i < arity; i++)
    compile_branch(scope, op->operands[i]);

  emit_byte(&scope->fn->code, op->line, (uint8_t)op->type);
}

static void block(FnScope *scope, Tnode *block, bool returns_val)
{
  scope->block_depth++;

  NodeList stmts = block->list;
  for (size_t i = 0; i < stmts.len; i++)
    compile_branch(scope, stmts.data[i]);

  Opcode op = returns_val ? OP_RETAIN1_DISCARDN : OP_DISCARDN;
  emit_size_op(&scope->fn->code, block->line, op, stmts.len);

  scope->block_depth--;
}

static void set(FnScope *scope, Str name, size_t line)
{
  Local *variable = resolve_local(scope, name);
  variable->initialized = true;
  emit_size_op(&scope->fn->code, line, OP_SET, variable->stack_slot);
}

static void get(FnScope *scope, Str name, size_t line)
{
  Local *variable = resolve_local(scope, name);
  if (!variable->initialized)
    runtime_error("invalid access: %s uninitialized", (int)name.len, name.s);
  emit_size_op(&scope->fn->code, line, OP_GET, variable->stack_slot);
}

// Compiles a branch of the AST.
static void compile_branch(FnScope *scope, Tnode *branch)
{
  switch (branch->type) {
  case AST_ASSIGN:
    compile_branch(scope, branch->operands[1]);
    set(scope, branch->operands[0]->ident, branch->line);
    return;
  case AST_BLOCK:
    block(scope, branch, true);
    return;
  case AST_CLOSED_BLOCK:
    block(scope, branch, false);
    return;
  case AST_LET:
    create_local(scope, branch->ident);
    return;
  default: break;
  }

  Op op = (Op)branch->type;
  if (is_unary_op(op))
    operator(scope, branch, 1);
  else if (is_binary_op(op))
    operator(scope, branch, 2);
}

// Compiles a function.
Proc *compile(Tnode *ast)
{
  FnScope *scope = malloc(sizeof(FnScope));
  if (scope == NULL) exit(EX_OSERR);

  scope->fn = proc_new();
  scope->enclosing_scope = NULL;
  scope->block_depth = 0;
  scope->locals = Locals_new();

  //compile_branch(scope, ast);
  return scope->fn;
}
