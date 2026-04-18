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
  sem.compound_assign_fn = NULL;
  sem.indent.initial = semantic(p)->indent.initial;
  sem.indent.continued = semantic(p)->indent.continued;
  sem.if_else_chained = false;
  sem.led_fail = false;
  sem.panic = semantic(p)->panic;

  return SemanticData_push(&p->semantic, sem);
}

static inline void end_semantic_scope(Parse *p)
{
  SemanticDatum sem = SemanticData_pop(&p->semantic);
  semantic(p)->indent.continued = sem.indent.continued;
}

static Local *create_local(Locals *locals,
    Str name, size_t depth, bool initialized)
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

static UpvalDesc *create_upval(Parse *p, Compiler *c, Str name)
{
  UpvalDesc upval = {name, .captures_local = false, .idx = 0};
  return ClosureDesc_push(p->vm, &c->procedure->closure_desc, upval);
}

static inline size_t upval_idx(Compiler *c, UpvalDesc *upval)
{
  return (size_t)(upval - c->procedure->closure_desc.data);
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

// Emit the end of a block, clearing stack slots
static void end_block(Parse *p, Token block_tok, size_t slots)
{
  p->c->stack_slot_count -= slots;
  if (slots == 0)
    emit_byte(p, block_tok, OP_RESERVE_SLOT);
  else if (slots > 1)
    emit_var_op(p, block_tok, OP_END_BLOCK, slots);
}

// Local variable lookup.
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

// Upvalue lookup.
// Create a new upvalue or reuse an old one
static UpvalDesc *resolve_upval(Parse *p, Compiler *c,
    Str name, UpvalDesc *upval)
{
  if (c->enclosing == NULL)
    return NULL;

  size_t idx;
  bool captures_local;
  Local *local;
  UpvalDesc *enclosing_upval;

  // Look at the enclosing function's locals.
  if ((local =
        resolve_local(c->enclosing, name)) != NULL) {
    // Upvalue to local variable slot
    local->is_captured = true;
    idx = local->stack_slot;
    captures_local = true;
  }

  // Look at the enclosing function's upvalues.
  else if ((enclosing_upval =
        resolve_upval(p, c->enclosing, name, NULL)) != NULL) {
    // Upvalue to another upvalue
    idx = upval_idx(c->enclosing, enclosing_upval);
    captures_local = false;
  }

  // Return NULL when unable to resolve the identifier.
  else return NULL;

  // Initialize & return the resolved upval.
  if (upval == NULL) upval = create_upval(p, c, name);
  upval->idx = idx;
  upval->captures_local = captures_local;
  return upval;
}

// Hoisted `let` declaration lookup.
static size_t resolve_deferred_let(Parse *p, Token tok)
{
  DeferredLet *deferred = &p->c->enclosing->deferred_let;

  // Try to recycle an existing deferred declaration
  for (size_t i = 0; i < deferred->len; i++)
    if (strs_eq(deferred->data[i].tok.slice, tok.slice))
      return upval_idx(p->c, deferred->data[i].upval);

  // Else, create a new upvalue that will be resolved at the end of the `let`.
  UpvalDesc *upval = create_upval(p, p->c, tok.slice);
  DeferredLet_push(deferred, (DeferredLookup){upval, tok});
  return upval_idx(p->c, upval);
}

// The first slots of a call frame are reserved for the function value itself
// and the parameters that were passed in.
static inline Locals arg_list_init(Str name)
{
  Locals arg_list = Locals_with_cap(1);
  create_local(&arg_list, name, 0, true); // Local representing the fn itself.
  return arg_list;
}

static void init_compiler(Parse *p, Locals arg_list)
{
  Compiler *c = allocate(NULL, sizeof(Compiler));

  c->depth = 0;
  c->locals = arg_list;
  c->stack_slot_count = arg_list.len;
  c->argc = arg_list.len - 1;

  c->loops = LoopStack_init();

  c->let_declaration = false;
  c->deferred_let = DeferredLet_init();

  // Switch compilers.
  // We're one function nesting level deeper.
  c->enclosing = p->c;
  p->c = c;
}

static void descend_compilers(Parse *p)
{
  Compiler *enclosing = p->c->enclosing;
  free(p->c->locals.data);
  free(p->c);
  p->c = enclosing;
}

// Return from compiler.
static Procedure *return_compiler(Parse *p)
{
  // Move arg list out of scope
  clear_local_scope(p);

  Procedure *procedure = p->c->procedure;
  GCList_pop(&p->vm->compiler_roots);

  // Return from procedure.
  emit_byte(p, p->current, OP_RETURN);
  descend_compilers(p);

  return procedure;
}

// Output the currently parsed p-code, otherwise defer all other compiler data
// to a later time
static Procedure *suspend_compiler(Parse *p)
{
  Procedure *procedure = p->c->procedure;
  GCList_pop(&p->vm->compiler_roots);

  // Continue execution later.
  emit_byte(p, p->current, OP_SUSPEND);

  p->c->procedure = NULL;
  return procedure;
}

Parse init_parse(Varmint *vm)
{
  Parse p;
  p.vm = vm;

  SemanticDatum sem;
  sem.assign_fn = sem.compound_assign_fn = NULL;
  sem.indent.initial = sem.indent.continued = 0;
  sem.if_else_chained = false;
  sem.led_fail = false;
  sem.panic = false;

  p.semantic = SemanticData_init();
  SemanticData_push(&p.semantic, sem);

  p.had_error = false;

  Locals initial_locals = arg_list_init(NULL_STR);
  p.c = NULL;
  init_compiler(&p, initial_locals);

  return p;
}

void free_parse(Parse *p)
{
  while (p->c != NULL) descend_compilers(p);
  free(p->semantic.data);
}

static void advance_lookahead(Parse *p);
static Token next(Parse *p);

static inline void init_new_code(Parse *p, String *source)
{
  if (source != NULL) {
    // Remember the new source string we're working with.
    p->source = source;

    // Initialize lex data
    p->lex = lex_new(source->s);
    // Token lookup.
    advance_lookahead(p); next(p);
  }
  else
    source = p->source;

  Value proc_val = Procedure_create(p->vm, p->c->argc, source);
  GCList_push(&p->vm->compiler_roots, proc_val);

  p->c->procedure = proc_val.as.procedure;
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

  error_line_snip(p->source->s, offending_tok.line,
               pointer ? (char *)offending_tok.slice.s : NULL);

  p->vm->status = VM_COMPILE_ERR;
  p->had_error = true;
  semantic(p)->panic = true;
}

static inline Token peek(Parse *p)
{
  return p->lookahead;
}

// Lex a new token into lookahead.
static void advance_lookahead(Parse *p)
{
  while (!p->lex.ended) {
    Token t = p->lookahead = lex_token(&p->lex);

    // Catch as many consecutive error tokens as possible.
    if (t.type != TK_ERR) break;
    parse_error(p, t, false, "%.*s", (int)t.slice.len, t.slice.s);
  }
}

static Token next(Parse *p)
{
  Token next_tok = p->lookahead;
  p->current = next_tok;
  advance_lookahead(p);
  return next_tok;
}

static inline Token eat(Parse *p)
{
  Token tok = p->current;
  next(p);
  return tok;
}

// Peek next token, skipping any lines in between
static Token peek_linewise(Parse *p)
{
  return p->current.type == TK_LINE ? peek(p) : p->current;
}

static inline void skip_line(Parse *p)
{
  if (p->current.type == TK_LINE) next(p);
}

static inline void skip_lookahead_line(Parse *p)
{
  if (peek(p).type == TK_LINE) advance_lookahead(p);
}

static void set_initial_line_indent(Parse *p, size_t indent)
{
  semantic(p)->indent.initial = indent;
  semantic(p)->indent.continued = 0;
}

static inline bool is_continued_line(Parse *p, size_t indent)
{
  return indent > semantic(p)->indent.initial
    && indent >= semantic(p)->indent.continued;
}

static bool set_continued_line_indent(Parse *p, size_t indent)
{
  if (!is_continued_line(p, indent))
    return false;
  else {
    semantic(p)->indent.continued = indent;
    return true;
  }
}

static void collapse_continued_line(Parse *p)
{
  Token tok = p->current;

  if (tok.type == TK_LINE) {
    size_t indent = tok.slice.len;

    if (set_continued_line_indent(p, indent))
      next(p);
  }
}

static bool match(Parse *p, TokenType expected)
{
  Token tok = peek_linewise(p);

  if (tok.type == expected) {
    if (p->current.type == TK_LINE) {
      set_initial_line_indent(p, p->current.slice.len);
      next(p);
    }
    if (tok.type != TK_EOF) next(p);
    return true;
  }
  else return false;
}

static Token consume(Parse *p, TokenType expected, char *const msg)
{
  Token tok = peek_linewise(p);

  if (tok.type != expected)
    parse_error(p, p->current, true, msg);

  if (p->current.type == TK_LINE) {
    set_initial_line_indent(p, p->current.slice.len);
    next(p);
  }
  if (tok.type != TK_EOF) next(p);
  return tok;
}

static bool match_op(Parse *p, TokenType expected)
{
  if (p->current.type == expected) {
    next(p);
    return true;
  }
  else if (p->current.type == TK_LINE) {
    size_t indent = p->current.slice.len;

    if (peek(p).type == expected && set_continued_line_indent(p, indent)) {
      next(p); next(p);
      return true;
    }
  }
  return false;
}

static const ParseRule *parse_rule(TokenType t);

static inline bool is_expr(TokenType type) // Null denoted expression
{
  return parse_rule(type)->nud != NULL || type == TK_LINE;
}

static inline bool is_led(TokenType type) // Left denotation
{
  return parse_rule(type)->led != NULL;
}

// Consume a null-denoted parse.
static inline void nud(Parse *p)
{
  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL)
    parse_error(p, lhs_token, true, "expect expression, got `%.*s`",
        (int)lhs_token.slice.len, lhs_token.slice.s);
  else
    lhs_rule(p);
}

static void juxtaposed(Parse *p, int min_bp);

// Consume left-denoted parses.
static void leds(Parse *p, int min_bp)
{
  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      if (is_expr(op_token.type)) op_rule = juxtaposed;
      else {
        parse_error(p, op_token, true, "expect operator, got `%.*s`",
            (int)op_token.slice.len, op_token.slice.s);

        next(p); continue; // Consume tokens until a valid operator is found.
      }
    }

    semantic(p)->panic = false; // Synchronize error state
    op_rule(p, min_bp);

    if (semantic(p)->led_fail)
      break; // Precedence too small or op_token otherwise cannot be a LED
  }
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
static void expr(Parse *p, int min_bp)
{
  new_semantic_scope(p);

  if (p->current.type == TK_LINE) {
    size_t indent = eat(p).slice.len;
    set_initial_line_indent(p, indent);
  }
  nud(p);
  leds(p, min_bp);

  end_semantic_scope(p);
}

// Parse the right hand side of an expression.
static inline void expr_rhs(Parse *p, Precedence prec, Associativity assoc)
{
  expr(p, (int)prec + (int)assoc);
}

// Parse an indented operator if one is found.
static void indentation(Parse *p, int min_bp)
{
  size_t indent = p->current.slice.len;

  if (!set_continued_line_indent(p, indent)) {
    semantic(p)->led_fail = true;
    return;
  }
  next(p); // Line token
  leds(p, min_bp);
}

static size_t consume_let_clauses(Parse *p, bool let_stmt);

// Parse a series of statements.
static void stmts(Parse *p, TokenType end, bool end_scope)
{
  new_semantic_scope(p);

  p->c->depth++;
  size_t slots = 0;

  // Consume statements
  while (!match(p, end)) {
    if (match(p, TK_LET))
      // `let` statement.
      slots += consume_let_clauses(p, true);

    else if (match(p, TK_SEMICOLON))
      // Delimiter.
      {}

    else {
      // Expression statement.
      if (!is_expr(p->current.type))
        parse_error(p, p->current, true, "invalid statement");

      expr(p, PREC_NONE);
      slots++;
      p->c->stack_slot_count++;
    }

    // Synchronize error state between statements.
    if (semantic(p)->panic) {
      for (TokenType t = p->current.type;
          t != TK_LINE && t != TK_EOF && t != end; t = next(p).type);

      semantic(p)->panic = false;
    }
  }

  if (end_scope) {
    clear_local_scope(p);
    end_block(p, p->current, slots);
  }
  p->c->depth--;

  end_semantic_scope(p);
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
static void emit_identifier(Parse *p, Token ident_tok,
    bool assign, bool access)
{
  Str ident = ident_tok.slice;

  Local *local = NULL;
  UpvalDesc *upval = NULL;

  size_t idx;
  Opcode get_op;
  bool initialized;
  semantic(p)->assigned_tok = ident_tok;
  semantic(p)->compound_assign_fn = NULL;

  if ((local = resolve_local(p->c, ident)) != NULL) {
    semantic(p)->assign_fn = assign_local;
    semantic(p)->assignable.local = local;
    idx = local->stack_slot;
    get_op = OP_GET;
    initialized = local->initialized;
  }

  else if (p->c->let_declaration) {
    // Assume the variable is further defined in the `let`.
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = idx
      = resolve_deferred_let(p, ident_tok);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else if ((upval = resolve_upval(p, p->c, ident, NULL)) != NULL) {
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = idx
      = upval_idx(p->c, upval);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else {
    parse_error(p, ident_tok, true, "undeclared variable %.*s",
        (int)ident.len, ident.s);
    return;
  }

  if (access) {
    // Access.
    if (initialized)
      emit_var_op(p, ident_tok, get_op, idx);
    else
      parse_error(p, ident_tok, true, "variable %.*s has not been initialized",
          (int)ident.len, ident.s);
  }
  if (!assign)
    semantic(p)->assign_fn = NULL;
}

static void elem_compound_assign(Parse *p)
{
  emit_bytes(p, semantic(p)->assigned_tok, 2,
      OP_DUP_2,
      OP_GET_ELEM);
}

static void elem_assign(Parse *p)
{
  emit_byte(p, semantic(p)->assigned_tok, OP_SET_ELEM);
}

// Emit p-code based on element access.
static void emit_elem(Parse *p, Token elem_tok, bool assign, bool access)
{
  semantic(p)->assigned_tok = elem_tok;

  semantic(p)->assign_fn = elem_assign;
  semantic(p)->compound_assign_fn = elem_compound_assign;

  if (access) {
    emit_byte(p, elem_tok, OP_GET_ELEM);
  }
  if (!assign)
    semantic(p)->assign_fn = NULL;
}

static bool peek_assignment(Parse *p)
{
  collapse_continued_line(p);
  TokenType current = p->current.type,
            next = peek(p).type;

  return current == TK_ASSIGN
    || (is_infix_op(current) && next == TK_ASSIGN);
}

// `:=` and `=` can easily be mixed, coming from other languages.
static bool match_assignment(Parse *p)
{
  if (match_op(p, TK_ASSIGN))
    return true;

  else {
    Token eq_tok = peek_linewise(p);
    if (match_op(p, TK_EQ))
      parse_error(p, eq_tok, true, "did you mean `:=`?");

    return false;
  }
}

// Assignment operator.
// Allows compound assignment shorthand `+:=`
// https://en.cppreference.com/w/c/language/operator_assignment.html#Compound_assignment
static inline void assignage(Parse *p, int min_bp, Opcode op_shorthand)
{
  if (PREC_ASSIGN < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  Token tok = eat(p); // op
  bool compound = op_shorthand != OP_NONE;

  if (compound) {
    next(p); // :=

    if (semantic(p)->compound_assign_fn != NULL)
      semantic(p)->compound_assign_fn(p);
  }

  expr_rhs(p, PREC_ASSIGN, ASSOC_RIGHT); // Parse and emit rhs.

  if (compound)
    // Emit compound opcode
    emit_byte(p, tok, (uint8_t)op_shorthand);

  // Emit assigning instruction.
  if (semantic(p)->assign_fn != NULL)
    semantic(p)->assign_fn(p);
  else
    parse_error(p, tok, true, "lhs is not assignable");
}

static void assign(Parse *p, int min_bp)
{
  assignage(p, min_bp, OP_NONE);
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

// Unary `+` is a no-op.
static void unary_plus(Parse *p)
{
  next(p);
  expr(p, PREC_SIGN);
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
  [TK_MOD]      = { OP_MODULO,   PREC_FACTOR, ASSOC_LEFT  },
  [TK_ARROW]    = { OP_I9N,      PREC_I9N,    ASSOC_LEFT  },
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

  if ((int)op.precedence < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  next(p); // Consume op_token

  // Parse right operand.
  expr_rhs(p, op.precedence, op.associativity);

  emit_byte(p, op_token, (uint8_t)op.type);
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT]   = { OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]      = { OP_FACTORIAL,  PREC_FACTORIAL },
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

// Left-denoted op tokens of ambiguous fixity.
// 50 % + 3
//    ^ Is the `%` infix or postfix?
static void led_op(Parse *p, int min_bp)
{
  Token op_token = p->current,
        next_token = peek(p);

  if (next_token.type == TK_ASSIGN)
    // Compound assignment.
    goto infix;

  bool indented = next_token.type == TK_LINE;
  size_t indent;

  if (indented) {
    indent = next_token.slice.len;

    advance_lookahead(p);
    next_token = peek(p);

    if (!is_continued_line(p, indent))
      // Next token isn't indented like a LED.
      goto infix;
  }

  TokenType op_t = op_token.type,
            next_t = next_token.type;

  if (!is_expr(next_t))
    // Next token is not a valid rhs expression.
    goto postfix;

  else if (!is_led(next_t))
    // Next token can't bind into this op.
    goto infix;

  // Next token can be an rhs or an infix op.
  // -> The one that binds the strongest is parsed as the infix op.
  BinaryOp op = infix_ops[op_t],
           next = infix_ops[next_t];

  int op_bp = (int)op.precedence + (int)op.associativity,
      next_bp = (int)next.precedence;

  if (op_bp > next_bp)
    goto infix;
  else
    goto postfix;

infix:
  // Infix operation. The next token is a NUD.
  infix_op(p, min_bp);
  return;

postfix:
  // Postfix operation. The next token is a LED.
  if (indented) semantic(p)->indent.continued = indent;
  postfix_op(p, min_bp);
}

static void cmp_chain(Parse *p, bool is_continuation)
{
  Token tok = eat(p);
  Opcode op = infix_ops[tok.type].type;

  // Parse right operand.
  expr_rhs(p, PREC_CMP, ASSOC_LEFT);

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
// a > b >= c /= d
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

static inline void grouping_end(Parse *p)
{
  expr(p, PREC_NONE);
  consume(p, TK_RPAREN, "expect grouping end"); // )
}

// { ... }
static void code_block(Parse *p)
{
  next(p); // {
  stmts(p, TK_RCURLY, true);
}

static void boolean(Parse *p)
{
  Token booltok = eat(p);
  int P = booltok.type == TK_TRUE;
  emit_constant(p, booltok, value_new(P, boolean));
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
    case TK_LCURLY: code_block(p); break;
    default:
      unreachable();
    }
  }

  emit_var_op(p, tok, OP_BUILD_STR, metas);
}

// Parse a delimited listing and return its length
static size_t delim_listing(Parse *p,
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

    expr(p, PREC_NONE);
    len++;

    if (match(p, end)) // ]
      return len;
  } while (match(p, delim)); // ,

  parse_error(p, p->current, true,
      "expect %s in listing", token_cstring(delim));
  next(p);
  consume(p, end, "expect listing end");
  return 0;
}

// [a, b, c]
static void list(Parse *p)
{
  Token bracket = p->current;
  size_t list_len = delim_listing(p, TK_LBRACK, TK_COMMA, TK_RBRACK, true);

  emit_var_op(p, bracket, OP_BUILD_LIST, list_len);
}

// Consume `.key` syntax of a table initializer.
static void table_ident_key(Parse *p)
{
  Token key = consume(p, TK_WORD, "expect table key");
  emit_constant(p, key,
      String_create(p->vm, key.slice.s, key.slice.len));
}

// Match element accessor syntax
static bool element_accessor(Parse *p)
{
  if (match(p, TK_DOT))
    // Identifier syntax.
    table_ident_key(p);

  else if (match(p, TK_LBRACK)) {
    // Subscript syntax.
    expr(p, PREC_NONE);
    consume(p, TK_RBRACK, "expect `]`");
  }

  else return false;
  return true;
}

static inline void table_entry(Parse *p, bool consumed_first_key)
{
  size_t nesting = consumed_first_key ? 1 : 0;

  // Consume consecutive keys
  for (; element_accessor(p); nesting++);

  // Consume value.
  consume(p, TK_ASSIGN, "expect `:=` after table key");
  expr_rhs(p, PREC_ASSIGN, ASSOC_RIGHT);

  // Emit nested entries.
  // .a.b["c"] := value
  if (nesting > 1)
    emit_var_op(p, p->current, OP_NESTED_TABLE_ENTRIES, nesting - 1);
}

// {.key := value}
static void table_end(Parse *p)
{
  // The first key has already been consumed.
  table_entry(p, true);
  size_t entry_count = 1;

  // Consume entries.
  for (; match(p, TK_COMMA); entry_count++) {
    if (peek_linewise(p).type == TK_RCURLY)
      break; // Trailing comma

    table_entry(p, false);
  }

  Token curly = consume(p, TK_RCURLY, "expect `}` after table initializer");
  // Emit table
  emit_var_op(p, curly, OP_BUILD_TABLE, entry_count);
}

static void some(Parse *p)
{
  Token some_tok = eat(p);
  consume(p, TK_LPAREN, "expect `(` after `Some`");

  expr(p, PREC_NONE);

  consume(p, TK_RPAREN, "expect `)` after `Some`");
  emit_byte(p, some_tok, OP_MAKE_SOME);
}

static void none(Parse *p)
{
  emit_byte(p, eat(p), OP_MAKE_NONE);
}

static void single_arg_maplet(Parse *p, Str arg);

// x
static void identifier(Parse *p)
{
  Token ident = eat(p);

  if (match_op(p, TK_MAPS_TO))
    // Maplet with a single argument.
    single_arg_maplet(p, ident.slice);

  else {
    // Identifier.
    bool access = peek_linewise(p).type != TK_ASSIGN;
    emit_identifier(p, ident, peek_assignment(p), access);
  }
}

// a.[i]
// tb.key
static void subscript(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  Token tok = p->current;
  element_accessor(p);

  bool access = peek_linewise(p).type != TK_ASSIGN;
  emit_elem(p, tok, peek_assignment(p), access);
}

// f(...)
static void invocation(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }
  Token paren = p->current;

  size_t arity = delim_listing(p,
      TK_LPAREN, TK_COMMA, TK_RPAREN, false); // Parse argument list.

  emit_var_op(p, paren, OP_CALL, arity);
}

// Two exprs placed next to each other marks a call with a single parameter.
// f x
static void juxtaposed(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }
  Token tok = p->current;

  expr_rhs(p, PREC_CALL, ASSOC_RIGHT);
  emit_byte(p, tok, OP_CALL_UNARY);
}

// a:b:f(...)
// https://en.wikipedia.org/wiki/Uniform_function_call_syntax
static void ufcs(Parse *p, int min_bp)
{
  if (PREC_UFCS < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  next(p); // ,
  size_t colon_count = 1;

  for (;;) {
    // The colon operator is right associative.
    // We parse with left though, to be able to grab the ensuing operator.
    expr_rhs(p, PREC_UFCS, ASSOC_LEFT);

    collapse_continued_line(p);
    Token tok = p->current;

    if (tok.type == TK_COLON) {
      // Continued UFCS
      next(p);
      colon_count++;
    }

    else {
      // Mirror the parameter list symmetrically before calling.
      emit_var_op(p, tok, OP_MIRROR, colon_count + 1);

      if (tok.type == TK_LPAREN) {
        // Direct function call.
        size_t params = delim_listing(p,
            TK_LPAREN, TK_COMMA, TK_RPAREN, false);
        emit_var_op(p, tok, OP_CALL, colon_count + params);
        break;
      }

      else if (is_expr(tok.type)) {
        // Single juxtaposed operand.
        expr_rhs(p, PREC_CALL, ASSOC_RIGHT);
        emit_var_op(p, tok, OP_CALL, colon_count + 1);
        break;
      }

      else {
        // Partial application with UFCS.
        emit_var_op(p, tok, OP_PARTIAL, colon_count);
      }

      break;
    }

  }
}

static Locals consume_arg_list(Parse *p, Str name)
{
  Locals arg_list = arg_list_init(name);

  if (peek_linewise(p).type == TK_RPAREN)
    return arg_list; // Nullary.

  do {
    Token arg_tok = consume(p, TK_WORD, "expect identifier");
    size_t arg_slot = arg_list.len;

    // Create argument local.
    create_local(&arg_list, arg_tok.slice, 0, true)
      ->stack_slot = arg_slot;
  } while (match(p, TK_COMMA) && peek_linewise(p).type == TK_WORD);

  return arg_list;
}

// Parse the body and emit a new function.
static void function(Parse *p, Str name, Locals arg_list, bool let_declaration)
{
  // Reserve first stack slot for the function value
  Value *fn_constant = emit_constant(p, p->current, NO_VALUE);

  init_compiler(p, arg_list);
  p->c->let_declaration = let_declaration;

  init_new_code(p, NULL);

  expr(p, PREC_NONE);

  Procedure *proc = return_compiler(p);
  proc->name = name;
  *fn_constant = value_new(proc, procedure);

  if (proc->closure_desc.len > 0)
    emit_byte(p, p->current, OP_CLOSURE); // Close function.
}

// (x, y) => ...
// https://en.wikipedia.org/wiki/Maps_to
static void maplet(Parse *p, Locals arg_list)
{
  function(p, NULL_STR, arg_list, false);
}

static void single_arg_maplet(Parse *p, Str arg)
{
  Locals arg_list = Locals_with_cap(2);

  create_local(&arg_list, NULL_STR, 0, true); // Fn local
  create_local(&arg_list, arg, 0, true)->stack_slot = 1; // Argument local

  maplet(p, arg_list);
}

// Function declaration.
// let f(x, y) := ...
static void fn_let(Parse *p, Str name)
{
  Locals arg_list = consume_arg_list(p, name);
  Token rparen = consume(p, TK_RPAREN, "expect argument list end");

  if (!match_assignment(p))
    parse_error(p, rparen, true, "function requires a body");

  function(p, name, arg_list, true);

  Local *fn_local = create_local_var(p, name);
  fn_local->initialized = true;
}

// Deferred name resolution.
static void let_resolution(Parse *p, Local *first_local)
{
  for (size_t i = 0; i < p->c->deferred_let.len; i++) {
    DeferredLookup *deferred = &p->c->deferred_let.data[i];
    Str ident = deferred->tok.slice;

    Local *deferred_local = NULL;

    // First try to resolve from the clauses bottom up
    for (Local *local = first_local, *top = Locals_top(&p->c->locals);
        local <= top;
        local++)
      if (strs_eq(ident, local->name)) {
        deferred_local = local;
        break;
      }

    if (deferred_local != NULL) {
      deferred->upval->idx = deferred_local->stack_slot;
      deferred->upval->captures_local = deferred_local->is_captured = true;
      continue;
    }

    // If that fails, try to resolve the upvalue.
    if (resolve_upval(p, p->c, ident, deferred->upval) != NULL)
      continue;

    parse_error(p, deferred->tok, true, "undeclared variable %.*s",
        (int)ident.len, ident.s);
  }

  p->c->deferred_let.len = 0;
}

// Consume comma-separated `let` declarations and return their total number
static size_t consume_let_clauses(Parse *p, bool let_stmt)
{
  size_t ndecls = 0;
  new_semantic_scope(p);

  Local *first_local = Locals_top(&p->c->locals) + 1; // `let` starts here

  do {
    Token ident_tok = peek_linewise(p);

    if (ident_tok.type != TK_WORD) {
      if (ndecls > 0 && (!let_stmt || peek_linewise(p).type == TK_IN))
        // Trailing comma is allowed in a `let` expression
        break;
      else
        // ...but not in a `let` statement.
        parse_error(p, ident_tok, true, "expect identifier");
    }

    skip_line(p);
    next(p); // Consume ident_tok

    if (match(p, TK_LPAREN))
      // This is an argument list.
      fn_let(p, ident_tok.slice);

    else {
      Local *local = create_local_var(p, ident_tok.slice);

      if (match_assignment(p)) {
        expr_rhs(p, PREC_NONE, ASSOC_NONE);
        local->initialized = true;
      }
      else
        emit_byte(p, ident_tok, OP_RESERVE_SLOT);
    }

    p->c->stack_slot_count++;
    ndecls++;
  } while (match(p, TK_COMMA));

  let_resolution(p, first_local);

  // `let` expressions require `in`, but you can also provide `in` after
  // a statement, turning it into an expression.
  if (!let_stmt) {
    consume(p, TK_IN, "expect `in` after `let` expression");
    goto end_expr;
  }
  else if (match(p, TK_IN)) goto stmt_as_expr;

  // Statement.
  end_semantic_scope(p);
  return ndecls;

stmt_as_expr:
  p->c->depth++;
  // Move local declarations into inner expression scope.
  for (Local *local = first_local, *top = Locals_top(&p->c->locals);
      local <= top;
      local++)
    local->depth++;

end_expr:
  expr(p, (int)PREC_TOP);
  clear_local_scope(p);

  end_block(p, p->current, ndecls + 1);
  p->c->depth--;
  return 1;
}

// The more functional and mathsy cousin of `let`.
// https://en.wikipedia.org/wiki/Let_expression
static void let_expr(Parse *p)
{
  next(p); // `let`
  p->c->depth++;
  consume_let_clauses(p, false);
}

static inline bool is_else(Parse *p)
{
  Token tok = peek_linewise(p);
  return tok.type == TK_ELSE || tok.type == TK_ELIF;
}

static void if_expr(Parse *p)
{
  Token if_tok = eat(p);

  // Parse condition.
  expr(p, PREC_NONE);
  size_t operand_idx = defer_op(p, if_tok, OP_IF);

  consume(p, TK_THEN, "expect `then` after `if`");
  // Parse conditional value.
  expr(p, PREC_IF);

  if (is_else(p)) {
    change_opcode(p, operand_idx, OP_JMP_WHEN_FALSE);

    // Allow else on the same indentation level as if
    skip_line(p);

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
  else { next(p); expr_rhs(p, PREC_ELSE, ASSOC_RIGHT); }

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
  loop.start = code_idx(p);

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
  // TODO: Broken
  abort();

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
  //construct_body(p, PREC_TOP, 0);
  loop->iter = code_idx(p);

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
  bool has_result = is_expr(p->current.type);
  if (has_result)
    expr_rhs(p, PREC_FLOW, ASSOC_LEFT); // Parse resulting value.
  else
    emit_byte(p, p->current, OP_RESERVE_SLOT);
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

// (...)
static void parens(Parse *p)
{
  next(p); // (

  if (match(p, TK_RPAREN)) {
    // This is a maplet with an empty argument list.
    consume(p, TK_MAPS_TO, "expect maplet arrow");
    maplet(p, arg_list_init(NULL_STR));
    return;
  }

  skip_line(p);
  skip_lookahead_line(p);

  if (p->current.type != TK_WORD) {
    grouping_end(p); return;
  }

  else switch (peek(p).type) {
  default:
    grouping_end(p); return;

  case TK_RPAREN:
    {
      // A single identifier in parentheses.
      Token ident_tok = eat(p);
      next(p);

      if (match(p, TK_MAPS_TO))
        single_arg_maplet(p, ident_tok.slice);
      else
        emit_identifier(p, ident_tok, false, true);
      return;
    }

  case TK_COMMA:
    break;
  }

  // We are in a grouping, looking at an identifier followed by a comma.
  // This has to be a maplet's argument list.
  Locals arg_list = consume_arg_list(p, NULL_STR);
  consume(p, TK_RPAREN, "expect argument list end");

  if (match_op(p, TK_MAPS_TO))
    maplet(p, arg_list);
  else {
    // Something must be wrong in the user's code.
    parse_error(p, p->current, true, "expect maplet arrow");
    free(arg_list.data);
  }
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
    [TK_EOF]       = { NULL,       led_end     },
    [TK_ERR]       = { NULL,       NULL        },

    [TK_LINE]      = { NULL,       indentation },

    [TK_PLUS]      = { unary_plus, infix_op    },
    [TK_MINUS]     = { prefix_op,  infix_op    },
    [TK_STAR]      = { NULL,       infix_op    },
    [TK_SLASH]     = { NULL,       infix_op    },
    [TK_CARET]     = { NULL,       infix_op    },
    [TK_PERCENT]   = { NULL,       led_op      },
    [TK_BANG]      = { NULL,       postfix_op  },
    [TK_2PIPE]     = { NULL,       infix_op    },

    [TK_EQ]        = { NULL,       cmp         },
    [TK_NEQ]       = { NULL,       cmp         },
    [TK_LT]        = { NULL,       cmp         },
    [TK_GT]        = { NULL,       cmp         },
    [TK_LEQ]       = { NULL,       cmp         },
    [TK_GEQ]       = { NULL,       cmp         },

    [TK_ASSIGN]    = { NULL,       assign      },
    [TK_LET]       = { let_expr,   NULL        },

    [TK_NOT]       = { prefix_op,  NULL        },
    [TK_AND]       = { NULL,       infix_op    },
    [TK_OR]        = { NULL,       infix_op    },

    [TK_IN]        = { NULL,       led_end     },

    [TK_MOD]       = { NULL,       infix_op    },

    [TK_IF]        = { if_expr,    NULL        },
    [TK_THEN]      = { NULL,       led_end     },
    [TK_ELSE]      = { NULL,       else_elif   },
    [TK_ELIF]      = { NULL,       else_elif   },

    [TK_LOOP]      = { loop_expr,  NULL        },
    [TK_FOR]       = { loop_expr,  NULL        },
    [TK_WHILE]     = { loop_expr,  NULL        },

    [TK_BREAK]     = { loop_flow,  NULL        },
    [TK_CONTINUE]  = { loop_flow,  NULL        },
    [TK_RETURN]    = { returnage,  NULL        },

    [TK_TRUE]      = { boolean,    NULL        },
    [TK_FALSE]     = { boolean,    NULL        },

    [TK_SOME]      = { some,       NULL        },
    [TK_NONE]      = { none,       NULL        },

    [TK_ARROW]     = { NULL,       infix_op    },
    [TK_MAPS_TO]   = { NULL,       NULL        },

    [TK_LPAREN]    = { parens,     invocation  },
    [TK_RPAREN]    = { NULL,       led_end     },

    [TK_LCURLY]    = { code_block, NULL        },
    [TK_RCURLY]    = { NULL,       led_end     },

    [TK_LBRACK]    = { list,       subscript   },
    [TK_RBRACK]    = { NULL,       led_end     },

    [TK_COLON]     = { NULL,       ufcs        },
    [TK_SEMICOLON] = { NULL,       led_end     },
    [TK_COMMA]     = { NULL,       led_end     },

    [TK_DOT]       = { NULL,       subscript   },
    [TK_DOTDOT]    = { NULL,       NULL        },

    [TK_NUMERAL]   = { number,     NULL        },

    [TK_STRCONT]   = { metastring, NULL        },
    [TK_STREND]    = { string,     NULL        },

    [TK_WORD]      = { identifier, NULL        },
    [TK_LABEL]     = { NULL,       led_end     },
 };

static const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

Procedure *compile(Varmint *vm, Parse *p, char *source)
{
  bool ad_hoc = p == NULL;
  String *source_string = String_own(vm, source).as.string;

  Parse new_parse;
  size_t initial_slot_count;

  if (ad_hoc) {
    // Embark on a brand new parse.
    new_parse = init_parse(vm); p = &new_parse;
  }
  else
    // Continue with the parsing we've been doing.
    initial_slot_count = p->c->stack_slot_count;

  init_new_code(p, source_string);

  // Parse program.
  if (peek_linewise(p).type == TK_EOF) {
    emit_byte(p, p->current, OP_RESERVE_SLOT);
    p->c->stack_slot_count++;
  }
  else
    stmts(p, TK_EOF, ad_hoc);

  Procedure *procedure;
  // Return from program
  if (ad_hoc) {
    procedure = return_compiler(p);
    free_parse(p);
  }
  else procedure = suspend_compiler(p);

  if (p->had_error) {
    procedure = NULL;

    if (!ad_hoc) {
      // Reset error state for the next run.
      p->had_error = semantic(p)->panic = false;

      // Delete erroneous top level locals
      while (Locals_top(&p->c->locals)->stack_slot >= initial_slot_count)
        Locals_pop(&p->c->locals);

      // Ignore any tallied slots
      p->c->stack_slot_count = initial_slot_count;
    }
  }

  return procedure;
}
