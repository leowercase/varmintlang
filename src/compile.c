#include "../inc/info.h"
#include "../inc/lex.h"
#include "../inc/code_gen.h"
#include "../inc/compile.h"
#include "../inc/val.h"

#include <stdarg.h>
#include <stdio.h>

static inline SemanticDatum *semantic(Parse *p)
{
  return SemanticData_top(&p->semantic);
}

static inline SemanticDatum *new_semantic_scope(Parse *p)
{
  SemanticDatum sem;
  sem.assign_fn = semantic(p)->assign_fn;
  sem.in_stmt = semantic(p)->in_stmt;
  sem.in_let_expr = semantic(p)->in_let_expr;
  sem.insert_semicolon = false;
  sem.if_else_chained = false;
  sem.led_fail = false;
  sem.panic = false;

  return SemanticData_push(&p->semantic, sem);
}

static inline void end_semantic_scope(Parse *p)
{
  SemanticDatum old_scope = SemanticData_pop(&p->semantic);
  semantic(p)->insert_semicolon =
    old_scope.insert_semicolon;
}

static void init_compiler(Parse *p, Locals arg_list)
{
  Compiler *c = allocate(NULL, sizeof(Compiler));
  c->depth = 0;

  Value proc_val = Procedure_create(p->vm, arg_list.len - 1);
  GCList_push(&p->vm->compiler_roots, proc_val);

  c->procedure = proc_val.as.procedure;

  c->locals = arg_list;
  c->stack_slot_count = arg_list.len;

  c->loops = LoopStack_init();

  c->let_declaration = false;
  c->deferred_let = DeferredLet_init();

  // Switch compilers.
  // We're one function nesting level deeper.
  c->enclosing = p->c;
  p->c = c;
}

static void clear_local_scope(Parse *p);

// Return from compiler.
static Procedure *return_compiler(Parse *p)
{
  // End arg list
  clear_local_scope(p);

  Procedure *procedure = p->c->procedure;
  GCList_pop(&p->vm->compiler_roots);

  // Return from procedure.
  emit_byte(p, p->current, OP_RETURN);

  Compiler *enclosing = p->c->enclosing;
  free(p->c->locals.data);
  free(p->c);
  p->c = enclosing;

  return procedure;
}

// Issue a parsing error and enter panic mode in the imminent semantic scope.
void parse_error(Parse *p, Token offending_tok, bool pointer,
    char *const msg, ...)
{
  if (semantic(p)->panic) return;

  error_out("[line %li] ", offending_tok.line);

  va_list args;
  va_start(args, msg);
  v_error_out(msg, args);
  va_end(args);
  error_out("\n");

  error_line_snip(p->vm->source, offending_tok.line,
               pointer ? (char *)offending_tok.slice.s : NULL);
  p->had_error = true;
  semantic(p)->panic = true;
}

// Emit the end of a block
static void end_block(Parse *p, Token block_tok, size_t slots, bool has_result)
{
  p->c->stack_slot_count -= slots;
  if (slots == 1 && has_result) return;

  Opcode opcode = has_result ? OP_END_BLOCK : OP_END_EMPTY_BLOCK;

  emit_var_op(p, block_tok, opcode, slots);
}

// Local lookup.
static Local *resolve_local(Compiler *c, Str name)
{
  if (c->locals.len == 0)
    return NULL;

  // Try to find a local in the current function.
  for (Local *local = Locals_top(&c->locals);
      local >= c->locals.data;
      local--)
    if (strs_eq(local->name, name))
      return local;

  return NULL;
}

static UpvalDesc *add_upval(Parse *p, Compiler *c,
    Str name, bool captures_local, size_t idx)
{
  UpvalDesc upval = {name, captures_local, idx};
  return ClosureDesc_push(p->vm, &c->procedure->closure_desc, upval);
}

static inline size_t upval_idx(Compiler *c, UpvalDesc *upval)
{
  return (size_t)(upval - c->procedure->closure_desc.data);
}

// Upvalue lookup.
static UpvalDesc *resolve_upval(Parse *p, Compiler *c, Str name)
{
  if (c->enclosing == NULL)
    return NULL;

  // Look at the enclosing function's locals.
  Local *local = resolve_local(c->enclosing, name);

  if (local != NULL) {
    local->is_captured = true;
    // Create new upvalue to that slot.
    return add_upval(p, c, name, true, local->stack_slot);
  }

  // Look at the enclosing function's upvalues.
  UpvalDesc *upval = resolve_upval(p, c->enclosing, name);
  if (upval == NULL) return NULL;

  // Create a new upvalue to that upvalue
  return add_upval(p, c, name, false, upval_idx(c->enclosing, upval));
}

// Hoisted `let` declaration lookup.
static size_t resolve_deferred_let(Parse *p, Token tok)
{
  DeferredLet *deferred = &p->c->enclosing->deferred_let;

  // Try to recycle an existing declaration
  for (size_t i = 0; i < deferred->len; i++)
    if (strs_eq(deferred->data[i].tok.slice, tok.slice))
      return upval_idx(p->c, deferred->data[i].upval);

  // Else, create a new upvalue.
  UpvalDesc *upval = add_upval(p, p->c, tok.slice, true, 0);
  DeferredLet_push(deferred, (DeferredLookup){upval, tok});
  return upval_idx(p->c, upval);
}

static Local *create_local(Locals *locals,
    Str name, int depth, bool initialized)
{
  Local local = {
    name, .stack_slot = 0, depth,
    initialized, .is_captured = false
  };
  return Locals_push(locals, local);
}

static Local *create_local_var(Parse *p, Str name)
{
  Local *local = create_local(&p->c->locals,
                      name, p->c->depth, false);
  // Because variables are declared in a stack-like manner,
  // we can predict the op stack slot they will occupy
  local->stack_slot = p->c->stack_slot_count;
  return local;
}

static void clear_local_scope(Parse *p)
{
  if (p->c->locals.len == 0)
    return;

  for (Local *local = Locals_top(&p->c->locals);
      local >= p->c->locals.data && local->depth == p->c->depth;
      local--) {
    if (local->is_captured)
      // Hoist upvalue.
      emit_byte(p, p->current, OP_HOIST_UPVALUE);

    Locals_pop(&p->c->locals);
  }
}

static inline Token peek(Parse *p)
{
  return p->lookahead;
}

static Token next(Parse *p)
{
  Token next_tok = p->lookahead;
  p->current = next_tok;
  for (;;) {
    if (p->lookahead.type == TK_EOF) break;
    Token t = p->lookahead = lex_token(&p->lex);

    // Catch as many consecutive error tokens as possible.
    if (t.type != TK_ERR) break;
    parse_error(p, t, false, "%.*s", (int)t.slice.len, t.slice.s);
  }
  return next_tok;
}

static inline Token eat(Parse *p)
{
  Token tok = p->current;
  next(p);
  return tok;
}

static bool match(Parse *p, TokenType expected)
{
  Token tok = p->current;
  if (tok.type != TK_EOF && tok.type == expected) {
    next(p);
    return true;
  }
  else return false;
}

static Token consume(Parse *p, TokenType expected, char *const msg)
{
  Token tok = p->current;
  if (!match(p, expected))
    parse_error(p, p->current, true, msg);
  return tok;
}

static const ParseRule *parse_rule(TokenType t);
static void expr(Parse *p, int min_bp);

static inline bool is_expr(Parse *p)
{
  return parse_rule(p->current.type)->nud != NULL;
}

typedef struct {
  Opcode type;
  Precedence precedence;
} UnaryOp;

typedef struct {
  Opcode type;
  Precedence precedence;
  Associativity associativity;
} BinaryOp;

static const UnaryOp prefix_ops[] = {
  [TK_MINUS] = { OP_NEGATE, PREC_SIGN },
  [TK_NOT] =   { OP_NOT,    PREC_NOT  },
};

static void prefix_op(Parse *p)
{
  Token op_tok = eat(p);
  UnaryOp op = prefix_ops[op_tok.type];

  expr(p, (int)op.precedence); // Parse and emit right operand.
  emit_byte(p, op_tok, (uint8_t)op.type);
}

// Allows compound assignment shorthand +:=
// https://en.cppreference.com/w/c/language/operator_assignment.html#Compound_assignment
static inline void assignage(Parse *p, int min_bp, Opcode op_shorthand)
{
  if (PREC_ASSIGN < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  Token tok = eat(p); // op
  bool compound = op_shorthand != OP_NONE;

  if (compound)
    next(p); // :=
    // Compound lhs has already been emitted.

  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  expr(p, r_bp); // Parse and emit rhs.

  if (compound)
    // Emit compound opcode
    emit_byte(p, tok, (uint8_t)op_shorthand);

  // Emit assigning instruction.
  if (semantic(p)->assign_fn != NULL)
    semantic(p)->assign_fn(p);
  else
    parse_error(p, p->current, true, "lhs is not assignable");
}

static void assign(Parse *p, int min_bp)
{
  assignage(p, min_bp, OP_NONE);
}

static const BinaryOp infix_ops[] = {
  [TK_PLUS]     = { OP_ADD,      PREC_TERM,   ASSOC_LEFT  },
  [TK_MINUS]    = { OP_SUB,      PREC_TERM,   ASSOC_LEFT  },
  [TK_STAR]     = { OP_MUL,      PREC_FACTOR, ASSOC_LEFT  },
  [TK_SLASH]    = { OP_DIV,      PREC_FACTOR, ASSOC_LEFT  },
  [TK_CARET]    = { OP_POW,      PREC_POWER,  ASSOC_RIGHT },
  [TK_PERCENT]  = { OP_MODULO,   PREC_FACTOR, ASSOC_LEFT  },
  [TK_2PIPE]    = { OP_CONCAT,   PREC_CONCAT, ASSOC_LEFT  },

  // Comparison is handled by `cmp`, not `infix_op`
  [TK_EQ]       = { OP_EQ,       PREC_CMP,    ASSOC_LEFT },
  [TK_NEQ]      = { OP_NEQ,      PREC_CMP,    ASSOC_LEFT },
  [TK_LT]       = { OP_LT,       PREC_CMP,    ASSOC_LEFT },
  [TK_LEQ]      = { OP_LEQ,      PREC_CMP,    ASSOC_LEFT },
  [TK_GT]       = { OP_GT,       PREC_CMP,    ASSOC_LEFT },
  [TK_GEQ]      = { OP_GEQ,      PREC_CMP,    ASSOC_LEFT },

  [TK_AND]      = { OP_AND,      PREC_AND,    ASSOC_LEFT  },
  [TK_OR]       = { OP_OR,       PREC_OR,     ASSOC_LEFT  },
  [TK_IN]       = { OP_IN,       PREC_IN,     ASSOC_LEFT  },
  [TK_NOTIN]    = { OP_NOTIN,    PREC_IN,     ASSOC_LEFT  },
  [TK_MOD]      = { OP_MODULO,   PREC_FACTOR, ASSOC_LEFT  },
  [TK_ARROW]    = { OP_I9N,      PREC_I9N,    ASSOC_LEFT  },

  [TK_DOTDOT]   = { OP_RANGE,    PREC_RANGE,  ASSOC_RIGHT },
  [TK_DOTDOTEQ] = { OP_RANGE_IN, PREC_RANGE,  ASSOC_RIGHT },
};

static void infix_op(Parse *p, int min_bp)
{
  Token op_token = p->current;
  BinaryOp op = infix_ops[op_token.type];

  // Assignment shorthand
  if (peek(p).type == TK_ASSIGN) {
    assignage(p, min_bp, op.type);
    return;
  }

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  next(p); // Consume op_token

  // Parse right operand.
  int r_bp = (int)l_bp + (int)op.associativity;
  expr(p, r_bp);

  emit_byte(p, op_token, (uint8_t)op.type);
}

static inline bool is_prefix_and_infix(TokenType op)
{
  // Can be extended later.
  return op == TK_PLUS || op == TK_MINUS;
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT]   = { OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]      = { OP_FACTORIAL,  PREC_FACTORIAL },
  [TK_UNWRAPPED] = { OP_UNWRAPPED,  PREC_ELSE      },
};

static void postfix_op(Parse *p, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  next(p); // Consume op_token
  emit_byte(p, op_token, (uint8_t)op.type);
}

static bool led_op_is_infix(TokenType op, TokenType next)
{
  const ParseRule *next_rule = parse_rule(next);

  if (next_rule->nud == NULL)
    return false; // Next token is not a valid rhs.

  else if (next_rule->led == NULL || !is_prefix_and_infix(next))
    return true; // Next token is not an infix op.

  else if (next == TK_ASSIGN)
    // Compound assignment.
    return true;

  else {
    int op_bp = (int)infix_ops[op].precedence +
                (int)infix_ops[op].associativity;

    int next_bp = (int)infix_ops[next].precedence +
                  (int)infix_ops[next].associativity;

    return op_bp < next_bp; // Battle of the binding powers!
  }
}

// LED op tokens of ambiguous fixity.
// 50% + 3
static void led_op(Parse *p, int min_bp)
{
  if (led_op_is_infix(p->current.type, peek(p).type))
    infix_op(p, min_bp);
  else
    postfix_op(p, min_bp);
}

static void cmp_chain(Parse *p, bool is_continuation)
{
  Token tok = eat(p);
  Opcode op = infix_ops[tok.type].type;

  // Parse right operand.
  const int r_bp = (int)PREC_CMP + (int)ASSOC_LEFT;
  expr(p, r_bp);

  bool is_chain = is_cmp_op(p->current.type);

  if (is_chain)
    // The rhs of this cmp acts as an lhs for the next one.
    // -> Duplicate rhs and swap it behind the current operands.
    emit_byte(p, tok, OP_SWAP_MOVE_OVER);

  // Emit operation.
  emit_byte(p, tok, (uint8_t)op);

  if (is_continuation) {
    // Move duplicate rhs to the background
    if (is_chain) emit_byte(p, tok, OP_SWAP_NEATH);

    // Comparisons are chained, logically conjunct.
    emit_byte(p, tok, OP_AND);
  }

  if (is_chain) {
    // Switch the places of the result and the swapped rhs.
    emit_byte(p, tok, OP_SWAP);
    cmp_chain(p, true);
  }
}

// Chained comparison operators.
// a > b >= c != d
static void cmp(Parse *p, int min_bp)
{
  if (PREC_CMP < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  // Assignment shorthand
  if (peek(p).type == TK_ASSIGN) {
    assignage(p, min_bp, infix_ops[p->current.type].type);
    return;
  }

  cmp_chain(p, false);
}

static void elem_assign(Parse *p)
{
  emit_byte(p, semantic(p)->assigned_tok, OP_ELEM_SET);
}

static void table_ident_key(Parse *p);

// a[i]
// tb.key
static void subscript(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  Token op_tok = eat(p); // [

  switch (op_tok.type) {
  case TK_LBRACK:
    expr(p, PREC_NONE);
    consume(p, TK_RBRACK, "unterminated subscript"); // ]
    break;
  case TK_DOT:
    table_ident_key(p);
    break;
  default:
    unreachable();
  }

  if (p->current.type != TK_ASSIGN) {
    bool compound =
        is_infix_op(p->current.type) && peek(p).type == TK_ASSIGN;

    if (compound)
      // Compound assign.
      emit_byte(p, op_tok, OP_DUP_2);

    // Access.
    emit_byte(p, op_tok, OP_ELEM_GET);

    if (!compound) return;
  }

  // Assign.
  if (semantic(p)->assign_fn == NULL)
    parse_error(p, op_tok, true, "invalid element assign");

  semantic(p)->assign_fn = elem_assign;
  semantic(p)->assigned_tok = op_tok;
}

static bool consume_arg_list_start(Parse *p)
{
  consume(p, TK_LPAREN, "expect grouping start");
  TokenType current = p->current.type,
            next = peek(p).type;

  return (current == TK_WORD && (next == TK_COMMA || next == TK_RPAREN))
       || current == TK_RPAREN;
}

// Returns argument locals
static Locals consume_arg_list(Parse *p, Str name)
{
  Locals arg_list = Locals_with_cap(1);
  create_local(&arg_list, name, 0, true); // Local representing the fn itself.

  // Parameters take up the first few stack slots of the frame.
  while (p->current.type == TK_WORD
      && (peek(p).type == TK_RPAREN || peek(p).type == TK_COMMA)) {
    size_t arg_slot = arg_list.len;
    // Create argument local.
    create_local(&arg_list, eat(p).slice, 0, true)
      ->stack_slot = arg_slot;

    if (!match(p, TK_COMMA)) break;
  }

  return arg_list;
}

// Parses a function body and creates a new function.
static void function(Parse *p, Str name, Locals arg_list, bool let_declaration)
{
  // Reserve first stack slot for the function value
  Value *fn_constant = emit_constant(p, p->current, NO_VALUE);

  init_compiler(p, arg_list);
  p->c->let_declaration = let_declaration;

  expr(p, PREC_NONE);

  Procedure *proc = return_compiler(p);
  proc->name = name;
  *fn_constant = value_new(proc, procedure);

  if (proc->closure_desc.len > 0)
    emit_byte(p, p->current, OP_CLOSURE); // Close function.
}

static void ident_str(Parse *p, Token ident_tok);

// (x, y) => ...
// https://en.wikipedia.org/wiki/Maps_to
static void maplet(Parse *p)
{
  Locals arg_list = consume_arg_list(p, NULL_STR);

  if (peek(p).type != TK_MAPS_TO) {
    // Doesn't match a maplet.
    size_t argc = arg_list.len - 1;

    if (argc == 0)
      // Need to parse expr inside grouping.
      expr(p, PREC_NONE);

    else if (argc == 1) {
      // Consumed a single identifier in paretheses - that needs to be emitted.
      // We already pushed it into locals in consume_arg_list
      Token ident_tok = {.line = p->current.line,
                         .slice = Locals_pop(&arg_list).name};
      ident_str(p, ident_tok);
    }

    else
      // Something is wrong in the user's code.
      parse_error(p, p->current, true,
          "expect maplet arrow after argument list");

    free(arg_list.data);
    consume(p, TK_RPAREN, "expect grouping end");
    return;
  }

  consume(p, TK_RPAREN, "expect argument list end");
  next(p); // =>
  function(p, NULL_STR, arg_list, false);
}

static inline void grouping_end(Parse *p)
{
  new_semantic_scope(p)->in_stmt = false;
  expr(p, PREC_NONE);
  end_semantic_scope(p);

  consume(p, TK_RPAREN, "expect grouping end"); // )
}

// (...)
static void grouping(Parse *p)
{
  if (consume_arg_list_start(p)) // (
    maplet(p);
  else
    grouping_end(p);
}

// Returns the length of the listing
static size_t delimited_listing(Parse *p,
    const TokenType start, const TokenType delim, const TokenType end,
    bool allow_trailing_delim)
{
  consume(p, start, "expect listing start"); // [
  size_t len = 0;

  // Consume listing elements
  do {
    if (match(p, end)) { // ]
      if (len == 0 || allow_trailing_delim)
        return len;
      else
        parse_error(p, p->current, true,
            "invalid trailing %s in listing", token_cstring(delim));
    }

    new_semantic_scope(p)->in_stmt = false;
    expr(p, PREC_NONE);
    len++;
    end_semantic_scope(p);

    if (match(p, end)) // ]
      return len;
  } while (match(p, delim)); // ,

  parse_error(p, p->current, true,
      "expect %s in listing", token_cstring(delim));
  next(p);
  consume(p, end, "expect listing end");
  return 0;
}

// f(...)
static void invocation(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }
  Token paren = p->current;

  size_t arity = delimited_listing(p,
      TK_LPAREN, TK_COMMA, TK_RPAREN, false); // Parse argument list.

  emit_var_op(p, paren, OP_CALL, arity);
}

// [a, b, c]
static void list(Parse *p)
{
  Token bracket = p->current;
  size_t list_len = delimited_listing(p, TK_LBRACK, TK_COMMA, TK_RBRACK, true);

  emit_var_op(p, bracket, OP_BUILD_LIST, list_len);
}

// Consume identifier syntax for table initialization
static void table_ident_key(Parse *p)
{
  Token key = consume(p, TK_WORD, "expect table key");
  emit_constant(p, key, String_create(p->vm, key.slice.s, key.slice.len));
}

// Using curly braces for tables _and_ code blocks can bring ambiguity.
static bool consume_table_start(Parse *p)
{
  next(p); // {

  // Empty table {}
  if (p->current.type == TK_RCURLY)
    return true;

  Token tok = p->current;

  // .key
  if (match(p, TK_DOT)) {
    table_ident_key(p);
    return true;
  }

  size_t list_len = 0;

  // ["key"]
  if (match(p, TK_LBRACK)) {
    expr(p, PREC_NONE);

    if (match(p, TK_RBRACK)) {
      if (p->current.type == TK_ASSIGN)
        return true;
      else
        // Consumed a list with a single element.
        list_len = 1;
    }
    else
      // Must be a list.
      list_len = delimited_listing(p, TK_COMMA, TK_COMMA, TK_RBRACK, true) + 1;
  }

  if (list_len > 0)
    // Emit the list we consumed.
    emit_var_op(p, tok, OP_BUILD_LIST, list_len);

  return false;
}

static void table_key(Parse *p)
{
  switch (eat(p).type) {
  case TK_DOT:
    // Identifier syntax.
    table_ident_key(p);
    break;
  case TK_LBRACK:
    // Subscript syntax.
    expr(p, PREC_NONE);
    consume(p, TK_RBRACK, "expect `]`");
    break;
  default:
    unreachable();
  }
}

static inline void table_value(Parse *p)
{
  consume(p, TK_ASSIGN, "expect `:=` after table key");

  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  expr(p, r_bp);
}

static inline void table_entry(Parse *p, bool consumed_first_key)
{
  size_t key_nesting =
    consumed_first_key ? 1 : 0;
  for (; p->current.type == TK_LBRACK || p->current.type == TK_DOT;
      key_nesting++)
    table_key(p);

  table_value(p);

  // Emit nested entries.
  // .a.b["c"] := value
  if (key_nesting > 1)
    emit_var_op(p, p->current, OP_NESTED_TABLE_ENTRIES, key_nesting - 1);
}

// {.key := value}
static void table(Parse *p)
{
  Token tok = p->current;

  if (match(p, TK_RCURLY)) {
    emit_byte(p, tok, OP_EMPTY_TABLE); // {}
    return;
  }

  // The first key has already been consumed.
  table_entry(p, true);
  size_t entry_count = 1;

  // Consume entries.
  if (match(p, TK_COMMA)) {
    do {
      if (p->current.type == TK_RCURLY)
        break; // Trailing comma.

      table_entry(p, false);
      entry_count++;
    } while (match(p, TK_COMMA));
  }

  consume(p, TK_RCURLY, "expect `}`");
  // Emit table
  emit_var_op(p, tok, OP_BUILD_TABLE, entry_count);
}

static void construct_body(Parse *p, Precedence prec, Associativity assoc)
{
  consume(p, TK_COLON, "expect `:`");
  int r_bp = (int)prec + (int)assoc;
  expr(p, r_bp);
}

static inline bool is_else(Parse *p)
{
  return p->current.type == TK_ELSE || p->current.type == TK_ELIF;
}

static void if_expr(Parse *p)
{
  Token if_tok = eat(p);

  // Parse condition.
  expr(p, PREC_NONE);
  size_t operand_idx = defer_op(p, if_tok, OP_IF);

  // Parse conditional value.
  construct_body(p, PREC_IF, 0);

  if (is_else(p)) {
    change_opcode(p, operand_idx, OP_JMP_WHEN_FALSE);

    semantic(p)->if_else_chained = true;
    semantic(p)->if_jmp_op_idx = operand_idx;
  }
  else {
    // Create an optional value.
    emit_byte(p, p->current, OP_MAKE_SOME);
    patch_jump(p, if_tok, operand_idx);
  }
}

static void else_elif(Parse *p, int min_bp)
{
  // else and elif are left-denoted operators.
  if (PREC_ELSE < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }
  Token else_tok = p->current;
  bool is_elif = else_tok.type == TK_ELIF;

  Opcode opcode =
    semantic(p)->if_else_chained ? OP_JMP
                                 : (is_elif ? OP_ELIF : OP_ELSE);
  size_t operand_idx =
    defer_op(p, else_tok, opcode);

  if (semantic(p)->if_else_chained)
    patch_jump(p, else_tok, semantic(p)->if_jmp_op_idx);

  semantic(p)->if_else_chained = false;

  if (is_elif) if_expr(p);
  else { next(p); construct_body(p, PREC_ELSE, ASSOC_RIGHT); }

  patch_jump(p, else_tok, operand_idx);
}

static Str loop_label(Parse *p)
{
  if (p->current.type == TK_LABEL) {
    Str label = eat(p).slice;
    label.s++; label.len--; // Cut out '
    return label;
  }
  else return NULL_STR;
}

static inline bool consume_comprehension(Parse *p)
{
  return match(p, TK_LBRACK) && match(p, TK_RBRACK);
}

static inline Loop *init_loop(Parse *p)
{
  Loop loop;

  loop.breaks = JumpIndices_init();
  loop.continues = JumpIndices_init();
  loop.start = code_top(p);

  loop.stack_slot = p->c->stack_slot_count;

  loop.label = loop_label(p);

  return LoopStack_push(&p->c->loops, loop);
}

static inline void end_loop(Parse *p, Token loop_tok, Loop *loop)
{
  // Patch instructions breaking out of the loop
  for (size_t i = 0; i < loop->breaks.len; i++)
    patch_jump(p, loop_tok, loop->breaks.data[i]);

  // ...And those continuing to the next iteration
  for (size_t i = 0; i < loop->continues.len; i++) {
    size_t continue_idx = loop->continues.data[i];
    patch_jump_to(p, loop_tok,
        continue_idx,
        // 2 accounts for the operands
        loop->iter - continue_idx - 2);
  }

  LoopStack_pop(&p->c->loops);
}

// loop for while
static void loop_expr(Parse *p)
{
  Token tok = eat(p);
  TokenType type = tok.type;

  // Loops can do something similar to Python list comprehension.
  // https://docs.python.org/3/tutorial/datastructures.html#list-comprehensions
  bool is_list_compre = consume_comprehension(p);

  Str for_var_ident;
  size_t for_counter_slot;
  // Initialize stack slots occupied before the loop
  if (type == TK_FOR) {
    for_var_ident = consume(p, TK_WORD, "expect identifier").slice;
    consume(p, TK_IN, "expect `in`");

    expr(p, PREC_NONE); // Iterable
    emit_byte(p, tok, OP_ZERO); // Counter
    for_counter_slot = p->c->stack_slot_count += 2;
  }

  // Initial value for the result of the last cycle.
  // Either an empty slot, or an empty list ready for comprehension
  if (is_list_compre) {
    emit_byte(p, tok, OP_LIST_COMPREHEND);
    p->c->stack_slot_count++;
  }
  else emit_byte(p, tok, OP_RESERVE_SLOT);

  // Loop start
  Loop *loop = init_loop(p);
  loop->is_for = type == TK_FOR;
  loop->is_list_compre = is_list_compre;

  // Emit conditional jump
  size_t jmp_idx = false;
  switch (type) {
  case TK_LOOP:
    // No conditional jump, but discard what the last cycle evaluated to
    if (!is_list_compre) emit_byte(p, tok, OP_POP);
    jmp_idx = false; break;

  case TK_WHILE:
    expr(p, PREC_NONE); // Condition
    jmp_idx = defer_op(p, tok,
        is_list_compre ? OP_WHILE_LIST : OP_WHILE); break;

  case TK_FOR:
    // Create loop variable
    create_local_var(p, for_var_ident)->initialized = true;
    p->c->stack_slot_count++;
    jmp_idx = defer_op(p, tok,
        is_list_compre ? OP_FOR_LIST : OP_FOR); break;

  default: unreachable();
  }

  // Parse loop body
  construct_body(p, PREC_TOP, 0);
  loop->iter = code_top(p);

  // Increment counter variable at the end of for
  if (type == TK_FOR)
    emit_var_op(p, tok, OP_FOR_INCREMENT, for_counter_slot);

  // Emit the looping instruction
  emit_loop(p, tok,
      is_list_compre ? OP_LOOP_LIST : OP_LOOP,
      loop->start);

  // Loop end
  end_loop(p, tok, loop);
  // Land the conditional jump here.
  if (jmp_idx) patch_jump(p, tok, jmp_idx);

  if (type == TK_FOR) {
    Locals_pop(&p->c->locals); // Loop variable
    p->c->stack_slot_count -= 3;
  }
  if (is_list_compre)
    p->c->stack_slot_count--;
}

static Loop *resolve_loop(Parse *p, Token control_flow)
{
  Token label_tok = p->current;
  Str label = loop_label(p);

  if (p->c->loops.len == 0) {
    parse_error(p, control_flow, true,
        "can only `%.*s` in a loop",
          (int)control_flow.slice.len, control_flow.slice.s);
    return NULL;
  }
  Loop *loop = LoopStack_top(&p->c->loops);

  if (label.s == NULL) return loop;
  // Find matching label
  for (; loop >= p->c->loops.data; loop--)
    if (strs_eq(label, loop->label)) return loop;

  parse_error(p, label_tok, true, "invalid label");
  return NULL;
}

// Emit the result of a control flow keyword
static void control_flow_result(Parse *p)
{
  bool has_result = is_expr(p);

  const int r_bp = (int)PREC_FLOW + (int)ASSOC_LEFT;
  if (has_result) expr(p, r_bp); // Parse resulting value.

  else emit_byte(p, p->current, OP_RESERVE_SLOT);
}

// break [value]
// continue [value]
static void loop_flow(Parse *p)
{
  Token tok = eat(p);

  Loop *loop = resolve_loop(p, tok);
  control_flow_result(p);

  if (loop == NULL) return;

  Opcode opcode;
  JumpIndices *worklist;

  // 1 accounts for the resulting value.
  size_t slots = p->c->stack_slot_count - loop->stack_slot + 1;

  if (tok.type == TK_CONTINUE && loop->is_for)
    slots--; // Don't discard the loop variable yet.

  // Discard the stack slots occupied.
  emit_var_op(p, tok, OP_END_BLOCK, slots);

  if (tok.type == TK_BREAK) {
    worklist = &loop->breaks;
    opcode = loop->is_list_compre ? OP_BREAK_LIST : OP_BREAK;

    if (loop->is_for)
      emit_byte(p, tok,
          loop->is_list_compre ? OP_DISCARD_FOR_LIST : OP_DISCARD_FOR);
  }
  else {
    worklist = &loop->continues;
    opcode = OP_JMP;
  }

  size_t jmp_idx = defer_op(p, tok, opcode);
  JumpIndices_push(worklist, jmp_idx);
}

// return [value]
static void returnage(Parse *p)
{
  Token tok = eat(p);
  control_flow_result(p);
  emit_byte(p, tok, OP_RETURN);
}

// using f, g, h: ...
static void using(Parse *p)
{
  Token tok = eat(p);
  p->c->depth++;

  size_t native_count = 0;
  do {
    Token ident_tok = consume(p, TK_WORD,
        "expect native function name in `using`");
    Str name = ident_tok.slice;

    Local *local = create_local_var(p, name);
    local->initialized = true;

    size_t *native_idx =
      NativesTable_get(&p->vm->natives_table, name);

    if (native_idx == NULL)
      parse_error(p, ident_tok, true,
          "no native function named %.*s", (int)name.len, name.s);
    else
      emit_constant(p, ident_tok, value_new(*native_idx, native));

    // Native function values occupy space too.
    native_count++;
    p->c->stack_slot_count++;
  } while (match(p, TK_COMMA));

  construct_body(p, PREC_TOP, 0);

  clear_local_scope(p);
  end_block(p, tok, native_count + 1, true);
  p->c->depth--;
}

static void boolean(Parse *p)
{
  Token booltok = eat(p);
  int P = booltok.type == TK_TRUE;
  emit_constant(p, booltok, value_new(P, boolean));
}

static void maybe_some(Parse *p)
{
  Token some_tok = eat(p);
  consume(p, TK_LPAREN, "expect `(` after `Some`");

  expr(p, PREC_NONE);

  consume(p, TK_RPAREN, "expect `)` after `Some`");
  emit_byte(p, some_tok, OP_MAKE_SOME);
}

static void maybe_none(Parse *p)
{
  emit_byte(p, eat(p), OP_MAKE_NONE);
}

static void number(Parse *p)
{
  Token ntok = eat(p);

  // Copying the slice to NUL-terminated so strtod doesn't parse anything extra
  String *nstring = String_create(p->vm,
      ntok.slice.s, ntok.slice.len).as.string;
  float64_t n = strtod(nstring->s, NULL);

  emit_constant(p, ntok, value_new(n, number));
}

static void string(Parse *p)
{
  Token strtok = eat(p);

  emit_constant(p, strtok,
      String_create(p->vm, strtok.slice.s, strtok.slice.len));
}

static void stmt_block_end(Parse *p, Token curly_tok);

static void metastring(Parse *p)
{
  size_t metas = 0;
  Token tok = p->current;

  for (bool found_end = false; !found_end; metas++) {
    switch (p->current.type) {
    case TK_STREND:
      found_end = true;
    case TK_STRCONT:
      if (p->current.slice.len == 0) {
        next(p); // Skip empty string tokens.
        metas--;
      }
      else string(p);
      break;
    case TK_LPAREN: next(p); grouping_end(p); break;
    case TK_LCURLY: stmt_block_end(p, eat(p)); break;
    default:
      unreachable();
    }
  }

  emit_var_op(p, tok, OP_BUILD_STR, metas);
}

static void assign_local(Parse *p)
{
  Local *local = semantic(p)->assignable.local;
  emit_var_op(p, semantic(p)->assigned_tok, OP_SET, local->stack_slot);
  local->initialized = true;
}

static void assign_upval(Parse *p)
{
  emit_var_op(p, semantic(p)->assigned_tok,
      OP_SET_UPVALUE, semantic(p)->assignable.upval_idx);
}

// Emit p-code based on an identifier occurrence.
static void ident_str(Parse *p, Token ident_tok)
{
  Str ident = ident_tok.slice;

  Local *local;
  UpvalDesc *upval;

  size_t idx;
  Opcode get_op;
  bool initialized;
  semantic(p)->assigned_tok = ident_tok;

  if ((local = resolve_local(p->c, ident)) != NULL) {
    semantic(p)->assign_fn = assign_local;
    semantic(p)->assignable.local = local;
    idx = local->stack_slot;
    get_op = OP_GET;
    initialized = local->initialized;
  }

  else if ((upval = resolve_upval(p, p->c, ident)) != NULL) {
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = idx
      = upval_idx(p->c, upval);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else if (p->c->let_declaration) {
    // Assume the variable is further defined in the `let`.
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = idx
      = resolve_deferred_let(p, ident_tok);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else {
    parse_error(p, ident_tok, true, "use of undeclared variable %.*s",
        (int)ident.len, ident.s);
    return;
  }

  if (p->current.type != TK_ASSIGN) {
    if (initialized)
      emit_var_op(p, ident_tok, get_op, idx);
    else
      parse_error(p, ident_tok, true, "variable %.*s has not been initialized",
          (int)ident.len, ident.s);
  }
}

// x
static void ident(Parse *p)
{
  ident_str(p, eat(p));
}

// Function declaration.
// let f(x, y) := ...
static void fn_decl(Parse *p, Str name)
{
  if (!consume_arg_list_start(p))
    parse_error(p, p->current, true, "expect argument list");

  Locals arg_list = consume_arg_list(p, name);
  consume(p, TK_RPAREN, "expect argument list end");

  consume(p, TK_ASSIGN, "function requires a body");
  function(p, name, arg_list, true);

  Local *fn_local = create_local_var(p, name);
  fn_local->initialized = true;
}

// Returns the number of `let` clauses consumed.
static size_t consume_lets(Parse *p)
{
  size_t ndecls = 0;
  new_semantic_scope(p);

  Local *first_local_decl = Locals_top(&p->c->locals) + 1;

  if (p->current.type != TK_WORD) {
    parse_error(p, p->current, true, "expect identifier");
    return 0;
  }

  do {
    Token ident_tok = eat(p);

    if (p->current.type == TK_LPAREN)
      // Matches an argument list for function declaration.
      fn_decl(p, ident_tok.slice);

    else {
      Local *local = create_local_var(p, ident_tok.slice);

      if (p->current.type == TK_EQ)
        parse_error(p, p->current, true, "did you mean `:=`?");

      if (match(p, TK_ASSIGN)) {
        const int assign_r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
        expr(p, assign_r_bp);
        local->initialized = true;
      }
      else
        emit_byte(p, ident_tok, OP_RESERVE_SLOT);
    }

    p->c->stack_slot_count++;
    ndecls++;
  } while (match(p, TK_COMMA) && p->current.type == TK_WORD);

  // Deferred name resolution.
  for (size_t i = 0; i < p->c->deferred_let.len; i++) {
    DeferredLookup *l = &p->c->deferred_let.data[i];
    Str ident = l->tok.slice;

    Local *deferred_local = NULL;
    // Resolve from bottom up.
    for (Local *local = first_local_decl, *top = Locals_top(&p->c->locals);
        local <= top;
        local++)
      if (strs_eq(ident, local->name)) {
        deferred_local = local;
        break;
      }

    if (deferred_local == NULL) {
      parse_error(p, l->tok, true, "use of undeclared variable %.*s",
          (int)ident.len, ident.s);
      continue;
    }

    l->upval->idx = deferred_local->stack_slot;
    l->upval->captures_local = deferred_local->is_captured = true;
  }
  p->c->deferred_let.len = 0;

  end_semantic_scope(p);
  return ndecls;
}

// The more functional and mathsy cousin of `let`.
// https://en.wikipedia.org/wiki/Let_expression
static void let_expr(Parse *p)
{
  p->c->depth++;
  Token let_tok = eat(p);

  new_semantic_scope(p)->in_let_expr = true;
  size_t ndecls = consume_lets(p);
  end_semantic_scope(p);

  consume(p, TK_IN, "expect `in` after `let` expression");
  construct_body(p, PREC_TOP, 0);

  clear_local_scope(p);
  end_block(p, let_tok, ndecls + 1, true);
  p->c->depth--;
}

// The `in` keyword is a bit of an ambiguous parse.
// It can denote an operator or the end of a `let` expression.
static void in_keyword(Parse *p, int min_bp)
{
  if (semantic(p)->in_let_expr && peek(p).type == TK_COLON)
    semantic(p)->led_fail = true;

  else infix_op(p, min_bp);
}

static size_t stmt(Parse *p)
{
  if (match(p, TK_LET))
    // let is a statement, as it requires stack semantics.
    return consume_lets(p);

  else {
    expr(p, PREC_NONE);
    p->c->stack_slot_count++;
    return 1;
  }
}

// A block is a series of statements.
// (The '{' has already been consumed.)
static void stmt_block_end(Parse *p, Token curly_tok)
{
  new_semantic_scope(p)->in_stmt = true;

  p->c->depth++;
  bool block_has_result = true;

  // Consume first statement
  size_t stmt_count = stmt(p);

  // Consume statements ...;
  while (!match(p, TK_RCURLY)) {
    // Consume tokens until a semicolon is found.
    while (!match(p, TK_SEMICOLON) && !semantic(p)->insert_semicolon) {
      Token tok = eat(p);
      if (tok.type == TK_EOF) goto end;
      parse_error(p, tok, true, "expect semicolon");
    }

    semantic(p)->insert_semicolon = false;
    semantic(p)->panic = false; // Synchronize error state between statements.

    if (match(p, TK_RCURLY)) {
      // Trailing semicolon, no value from block expr.
      block_has_result = false;
      break;
    }

    if (match(p, TK_RCURLY)) break;

    stmt_count += stmt(p);
  }

end:
clear_local_scope(p);
end_block(p, curly_tok, stmt_count, block_has_result);
p->c->depth--;

  end_semantic_scope(p);
}

// { ... }
static void block(Parse *p)
{
  Token curly_tok = p->current;

  if (consume_table_start(p))
    table(p);
  else
    stmt_block_end(p, curly_tok);
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
static void expr(Parse *p, int min_bp)
{
  new_semantic_scope(p);

  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL)
    parse_error(p, lhs_token, true, "expect expression, got `%.*s`",
        (int)lhs_token.slice.len, lhs_token.slice.s);
  else
    lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      if (semantic(p)->in_stmt) {
        if (semantic(p)->insert_semicolon) break;

        parse_error(p, op_token, true, "unexpected `%.*s`, did you mean to add `;`?",
            (int)op_token.slice.len, op_token.slice.s);

        semantic(p)->insert_semicolon = true; break; // "Insert" semicolon.
      }

      parse_error(p, op_token, true, "expect operator, got `%.*s`",
          (int)op_token.slice.len, op_token.slice.s);

      next(p); continue; // Consume tokens until a valid operator is found.
    }

    semantic(p)->panic = false; // Synchronize error state after lhs
    op_rule(p, min_bp);

    if (semantic(p)->led_fail)
      break; // Precedence too small or op_token otherwise cannot be a LED
  }

  end_semantic_scope(p);
}

// Token denoting the end of a surrounding.
static void led_end(Parse *p, int _)
{
  // Skip further LED parsing at this depth.
  semantic(p)->led_fail = true;
}

static const ParseRule parse_rules[] =
  {
/*  token type         NUD         LED        */
    [TK_EOF]       = { NULL,       led_end    },
    [TK_ERR]       = { NULL,       NULL       },

    [TK_PLUS]      = { prefix_op,  infix_op   },
    [TK_MINUS]     = { prefix_op,  infix_op   },
    [TK_STAR]      = { NULL,       infix_op   },
    [TK_SLASH]     = { NULL,       infix_op   },
    [TK_CARET]     = { NULL,       infix_op   },
    [TK_PERCENT]   = { NULL,       led_op     },
    [TK_BANG]      = { NULL,       postfix_op },
    [TK_2PIPE]     = { NULL,       infix_op   },

    [TK_EQ]        = { NULL,       cmp        },
    [TK_NEQ]       = { NULL,       cmp        },
    [TK_LT]        = { NULL,       cmp        },
    [TK_GT]        = { NULL,       cmp        },
    [TK_LEQ]       = { NULL,       cmp        },
    [TK_GEQ]       = { NULL,       cmp        },

    [TK_ASSIGN]    = { NULL,       assign     },
    [TK_LET]       = { let_expr,   NULL       },

    [TK_NOT]       = { prefix_op,  NULL       },
    [TK_AND]       = { NULL,       infix_op   },
    [TK_OR]        = { NULL,       infix_op   },
    [TK_IN]        = { NULL,       in_keyword },
    [TK_NOTIN]     = { NULL,       infix_op   },

    [TK_MOD]       = { NULL,       infix_op   },

    [TK_UNWRAPPED] = { NULL,       postfix_op },

    [TK_IF]        = { if_expr,    NULL       },
    [TK_ELSE]      = { NULL,       else_elif  },
    [TK_ELIF]      = { NULL,       else_elif  },

    [TK_LOOP]      = { loop_expr,  NULL       },
    [TK_FOR]       = { loop_expr,  NULL       },
    [TK_WHILE]     = { loop_expr,  NULL       },

    [TK_BREAK]     = { loop_flow,  NULL       },
    [TK_CONTINUE]  = { loop_flow,  NULL       },
    [TK_RETURN]    = { returnage,  NULL       },

    [TK_USING]     = { using,      NULL       },

    [TK_TRUE]      = { boolean,    NULL       },
    [TK_FALSE]     = { boolean,    NULL       },

    [TK_SOME]      = { maybe_some, NULL       },
    [TK_NONE]      = { maybe_none, NULL       },

    [TK_ARROW]     = { NULL,       infix_op   },
    [TK_MAPS_TO]   = { NULL,       NULL       },

    [TK_LPAREN]    = { grouping,   invocation },
    [TK_RPAREN]    = { NULL,       led_end    },

    [TK_LCURLY]    = { block,      NULL       },
    [TK_RCURLY]    = { NULL,       led_end    },

    [TK_LBRACK]    = { list,       subscript  },
    [TK_RBRACK]    = { NULL,       led_end    },

    [TK_COLON]     = { NULL,       led_end    },
    [TK_SEMICOLON] = { NULL,       led_end    },
    [TK_COMMA]     = { NULL,       led_end    },

    [TK_DOT]       = { NULL,       subscript  },

    [TK_DOTDOT]    = { NULL,       infix_op   },
    [TK_DOTDOTEQ]  = { NULL,       infix_op   },

    [TK_NUMERAL]   = { number,     NULL       },

    [TK_STRCONT]   = { metastring, led_end    },
    [TK_STREND]    = { string,     led_end    },

    [TK_WORD]      = { ident,      NULL       },
    [TK_LABEL]     = { NULL,       led_end    },
 };

static const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

static Parse init_parse(Varmint *vm, char *source)
{
  Parse p;
  p.vm = vm;

  p.lex = lex_new(source);

  p.had_error = false;
  p.semantic = SemanticData_init();

  Locals initial_locals = Locals_with_cap(1);
  // Reserve first stack slot for the program value.
  create_local(&initial_locals, NULL_STR, 0, false);

  p.c = NULL;
  init_compiler(&p, initial_locals);
  return p;
}

Procedure *compile(Varmint *vm, char *source)
{
  if (*source == '\0') {
    error_out("empty file\n");
    return NULL;
  }

  Parse p = init_parse(vm, source);

  SemanticDatum sem;
  sem.assign_fn = NULL;
  sem.in_stmt = false;
  sem.in_let_expr = false;
  sem.insert_semicolon = false;
  sem.if_else_chained = false;
  sem.led_fail = false;
  sem.panic = false;
  SemanticData_push(&p.semantic, sem);

  next(&p); next(&p);
  expr(&p, PREC_NONE);

  free(p.semantic.data);

  Procedure *proc = return_compiler(&p);
  return p.had_error ? NULL : proc;
}
