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
  sem.assign_fn = sem.compound_assign_fn = NULL;
  sem.indent.initial = semantic(p)->indent.initial;
  sem.indent.continued = semantic(p)->indent.continued;
  sem.else_chained = NULL;
  sem.is_stmts = false;
  sem.in_stmts = semantic(p)->is_stmts;
  sem.led_end = false;
  sem.panic = semantic(p)->panic;

  return SemanticData_push(&p->semantic, sem);
}

static inline void end_semantic_scope(Parse *p)
{
  SemanticDatum sem = SemanticData_pop(&p->semantic);
  // Expressions inherit indentation.
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
  // Because local variables are declared in a LIFO manner,
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

// Returns the index of the hoisted `var` upvalue in the upvalues array.
static size_t deferred_var_idx(Parse *p, Token tok)
{
  DeferredVar *deferred_var = &p->c->enclosing->deferred_var;

  // Create a new upvalue that will be resolved at the end of the `var`.
  UpvalDesc *upval = create_upval(p, p->c, tok.slice);
  DeferredVar_push(deferred_var, (DeferredLookup){upval, tok});
  return upval_idx(p->c, upval);
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

static UpvalDesc *resolve_upval(Parse *p, Compiler *c, Str name);

// Upvalue lookup.
static UpvalDesc *resolve_upval_from(Parse *p,
    Compiler *enclosing, UpvalDesc *upval)
{
  if (enclosing == NULL)
    return NULL;

  size_t idx;
  bool captures_local;
  Local *local;
  UpvalDesc *enclosing_upval;

  // Look at the enclosing function's locals.
  if ((local = resolve_local(enclosing, upval->name))
      != NULL) {
    // Upvalue to local variable slot
    idx = local->stack_slot;
    captures_local = local->is_captured = true;
  }

  // Look at the enclosing function's upvalues.
  else if ((enclosing_upval = resolve_upval(p, enclosing, upval->name))
      != NULL) {
    // Upvalue to another upvalue
    idx = upval_idx(enclosing, enclosing_upval);
    captures_local = false;
  }

  // Return NULL when unable to resolve the identifier.
  else return NULL;

  // Initialize & return the resolved upval.
  upval->idx = idx;
  upval->captures_local = captures_local;
  return upval;
}

static UpvalDesc *resolve_upval(Parse *p, Compiler *c, Str name)
{
  UpvalDesc *upval = create_upval(p, c, name);
  return resolve_upval_from(p, c->enclosing, upval);
}

static void clear_local(Parse *p)
{
  Local local = Locals_pop(&p->c->locals);

  if (local.is_captured)
    // Hoist upvalue.
    emit_byte(p, p->current, OP_HOIST_UPVALUE);
}

static void clear_local_scope(Parse *p)
{
  Locals *locals = &p->c->locals;

  while (locals->len > 0 && Locals_top(locals)->depth == p->c->depth)
    clear_local(p);
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

// The first slots of a call frame are reserved for the function value itself
// and the parameters that were passed in.
static inline Locals arg_list_init(Str name)
{
  Locals arg_list = Locals_with_cap(1);
  create_local(&arg_list, name, 0, true)
    ->stack_slot = 0; // Local representing the fn itself.
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

  c->var_declaration = false;
  c->deferred_var = DeferredVar_init();

  // Switch compilers.
  // We're one function nesting level deeper.
  c->enclosing = p->c;
  p->c = c;
}

static void descend_compilers(Parse *p)
{
  Compiler *enclosing = p->c->enclosing;

  free(p->c->locals.data);
  free(p->c->deferred_var.data);
  free(p->c->loops.data);

  free(p->c);
  p->c = enclosing;
}

static void emit_return(Parse *p, bool early_return)
{
  // Discard stack slots on early return.
  if (early_return)
    emit_byte(p, p->current, OP_END_SLOTS);

  // Return from procedure.
  emit_byte(p, p->current, OP_RETURN);
}

// Return from compiler.
static Procedure *return_compiler(Parse *p)
{
  // Move arg list out of scope
  clear_local_scope(p);

  Procedure *procedure = p->c->procedure;
  GCList_pop(&p->vm->compiler_roots);

  emit_return(p, false);
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
  sem.else_chained = NULL;
  sem.is_stmts = sem.in_stmts = false;
  sem.led_end = false;
  sem.panic = false;

  p.semantic = SemanticData_init();
  SemanticData_push(&p.semantic, sem);

  p.had_error = false;

  p.c = NULL;
  init_compiler(&p, arg_list_init(NULL_STR));

  // Builtin locals
  for (size_t i = 0; i < vm->builtins.len; i++) {
    Str name = vm->builtins.data[i].name;

    create_local_var(&p, name)->initialized = true;
    p.c->stack_slot_count++;
  }

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

static inline bool is_nud(TokenType type) // Null denotation
{
  return parse_rule(type)->nud != NULL;
}

static inline bool is_led(TokenType type) // Left denotation
{
  return parse_rule(type)->led != NULL;
}

static inline bool is_expr(Parse *p, Token token)
{
  if (token.type == TK_LINE)
    return is_continued_line(p, token.slice.len);
  else
    return is_nud(token.type);
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
      if (is_expr(p, op_token)) op_rule = juxtaposed;
      else {
        parse_error(p, op_token, true, "expect operator, got `%.*s`",
            (int)op_token.slice.len, op_token.slice.s);

        next(p); continue; // Consume tokens until a valid operator is found.
      }
    }

    semantic(p)->panic = false; // Synchronize error state
    op_rule(p, min_bp);

    if (semantic(p)->led_end)
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
    semantic(p)->led_end = true;
    return;
  }
  next(p); // Line token
  leds(p, min_bp);
}

// Parse a series of statements.
static void stmts(Parse *p, TokenType end, bool end_scope)
{
  SemanticDatum *scope = new_semantic_scope(p);
  scope->is_stmts = true;
  scope->statement_count = 0;

  p->c->depth++;

  // Consume statements
  while (!match(p, end)) {
    if (match(p, TK_SEMICOLON)); // Delimiter.

    else if (match(p, TK_EOF))
      parse_error(p, p->current, false, "expect end of statements");

    else {
      // Expression statement.

      if (p->current.type == TK_LINE) {
        set_initial_line_indent(p, p->current.slice.len);
        next(p);
      }

      if (!is_nud(p->current.type))
        parse_error(p, p->current, true, "invalid statement");

      expr(p, PREC_NONE);
      semantic(p)->statement_count++;
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
    end_block(p, p->current, semantic(p)->statement_count);
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
  Str identifier = ident_tok.slice;

  Local *local = NULL;
  UpvalDesc *upval = NULL;

  Opcode get_op;
  size_t operand;
  bool initialized;
  semantic(p)->assigned_tok = ident_tok;
  semantic(p)->compound_assign_fn = NULL;

  if ((local = resolve_local(p->c, identifier)) != NULL) {
    // The identifier refers to a slot on the operation stack.
    semantic(p)->assign_fn = assign_local;
    semantic(p)->assignable.local = local;
    operand = local->stack_slot;
    get_op = OP_GET;
    initialized = local->initialized;
  }

  else if (p->c->var_declaration) {
    // For now assume the variable is further defined in the `var`.
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = operand
      = deferred_var_idx(p, ident_tok);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else if ((upval = resolve_upval(p, p->c, identifier)) != NULL) {
    // Closed over variable.
    semantic(p)->assign_fn = assign_upval;
    semantic(p)->assignable.upval_idx = operand
      = upval_idx(p->c, upval);
    get_op = OP_GET_UPVALUE;
    initialized = true;
  }

  else {
    parse_error(p, ident_tok, true, "undeclared variable %.*s",
        (int)identifier.len, identifier.s);
    return;
  }

  if (access) {
    // Access.
    if (initialized)
      emit_var_op(p, ident_tok, get_op, operand);
    else
      parse_error(p, ident_tok, true, "variable %.*s has not been initialized",
          (int)identifier.len, identifier.s);
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

  if (access)
    emit_byte(p, elem_tok, OP_GET_ELEM);

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

  if (peek(p).type == TK_ASSIGN) {
    // Compound assignment
    assignage(p, min_bp, op.type);
    return;
  }

  if ((int)op.precedence < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  next(p); // Consume op_token

  // Parse right operand.
  expr_rhs(p, op.precedence, op.associativity);

  emit_byte(p, op_token, (uint8_t)op.type);
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT]     = { OP_PERCENTAGE,  PREC_PERCENT   },
  [TK_BANG]        = { OP_FACTORIAL,   PREC_FACTORIAL },
};

static void postfix_op(Parse *p, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp) {
    semantic(p)->led_end = true;
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

  if (!is_expr(p, next_token))
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

// Chainable comparison operators.
// a > b >= c /= d
static void cmp(Parse *p, int min_bp)
{
  if (PREC_CMP < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  if (peek(p).type == TK_ASSIGN) {
    // Compound assignment
    assignage(p, min_bp, infix_ops[p->current.type].type);
    return;
  }

  cmp_chain(p, false);
}

static inline void grouping_end(Parse *p)
{
  expr(p, PREC_NONE);
  consume(p, TK_RPAREN, "expect grouping end");
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
        parse_error(p, p->current, true, "invalid trailing delimiter");
    }

    expr(p, PREC_NONE);
    len++;

    if (match(p, end)) // ]
      return len;
  } while (match(p, delim)); // ,

  parse_error(p, p->current, true, "expect listing delimiter");
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

// .key
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

// [key] := value
static void table_entry(Parse *p)
{
  size_t key_nesting = 0;

  Token ident_tok;
  if ((ident_tok = peek_linewise(p)).type == TK_WORD) {
    // First identifier key occurrence is without a `.` prefix
    key_nesting++;
    table_ident_key(p);

    TokenType next = peek_linewise(p).type;
    if (next == TK_COMMA || next == TK_RBRACK) {
      // Identifier shorthand.
      emit_identifier(p, ident_tok, false, true);
      return;
    }
  }

  // Consume consecutive keys
  while (element_accessor(p)) key_nesting++;

  // Consume value.
  consume(p, TK_ASSIGN, "expect `:=` after table key");
  expr_rhs(p, PREC_ASSIGN, ASSOC_RIGHT);

  // Emit nested entries.
  // a.b.c["d"] := value
  if (key_nesting > 1)
    emit_var_op(p, p->current, OP_NESTED_TABLE_ENTRIES, key_nesting - 1);
}

static void table(Parse *p)
{
  next(p); // @[
  size_t entry_count = 0;

  // Consume entries.
  do {
    TokenType t = peek_linewise(p).type;
    if (t != TK_WORD && t != TK_LBRACK) break; // Trailing comma or `]`.

    table_entry(p);
    entry_count++;
  } while (match(p, TK_COMMA));

  Token curly = consume(p, TK_RBRACK,
      "expect `]` after table initializer");

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

// a[i]
// tb.key
static void subscript(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  Token tok = p->current;
  element_accessor(p);

  bool access = peek_linewise(p).type != TK_ASSIGN;
  emit_elem(p, tok, peek_assignment(p), access);
}

// a?[i]
// tb?.key
static void q_subscript(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  Token tok = eat(p);

  if (tok.type == TK_Q_DOT)
    // Identifier syntax.
    table_ident_key(p);

  else if (tok.type == TK_Q_LBRACK) {
    // Subscript syntax.
    expr(p, PREC_NONE);
    consume(p, TK_RBRACK, "expect `]`");
  }

  emit_byte(p, tok, OP_MAYBE_GET_ELEM);
}

// result?!
static void interrobang(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  Token tok = next(p); // ?!
  size_t operand_idx = defer_op(p, tok, OP_ELSE);

  emit_byte(p, tok, OP_MAKE_NONE);
  emit_return(p, true);

  patch_jump(p, tok, operand_idx);
}

// f(...)
static void invocation(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_end = true;
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
    semantic(p)->led_end = true;
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
    semantic(p)->led_end = true;
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

      else if (is_expr(p, tok)) {
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
static void function(Parse *p, Str name, Locals arg_list, bool var_declaration)
{
  // Reserve first stack slot for the function value
  Value *fn_constant = emit_constant(p, p->current, NO_VALUE);

  init_compiler(p, arg_list);
  p->c->var_declaration = var_declaration;

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

  create_local(&arg_list, NULL_STR, 0, true)->stack_slot = 0; // Fn local
  create_local(&arg_list, arg, 0, true)->stack_slot = 1; // Argument local

  maplet(p, arg_list);
}

// Function declaration.
// var f(x, y) := ...
static void fn_var(Parse *p, Str name)
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
static void resolve_var(Parse *p, Local *locals, size_t ndecls)
{
  for (size_t i = 0, n = p->c->deferred_var.len;
      i < n; i++) {
    DeferredLookup *deferred = &p->c->deferred_var.data[i];
    Str identifier = deferred->tok.slice;

    Local *deferred_decl = NULL;

    // First try to resolve from the variables the clauses declare, bottom up
    for (size_t j = 0; j < ndecls; j++)
      if (strs_eq(identifier, locals[j].name)) {
        deferred_decl = &locals[j];
        break;
      }

    if (deferred_decl != NULL) {
      deferred->upval->idx = deferred_decl->stack_slot;
      deferred->upval->captures_local = deferred_decl->is_captured = true;
      continue;
    }

    // If that fails, resolve the identifier just like any other upvalue.
    if (resolve_upval_from(p, p->c, deferred->upval) != NULL)
      continue;

    parse_error(p, deferred->tok, true, "undeclared variable %.*s",
        (int)identifier.len, identifier.s);
  }

  p->c->deferred_var.len = 0;
}

// `var` declares variables.
// Can be a statement or an expression.
// https://en.wikipedia.org/wiki/Let_expression
static void var_bind(Parse *p)
{
  next(p); // `var`

  bool is_statement = semantic(p)->in_stmts;
  size_t decl_count = 0;

  // Declarations start here
  size_t locals_idx = p->c->locals.len;
  p->c->depth++;

  for (;;) {
    Token ident_tok = consume(p, TK_WORD, "expect identifier");

    if (match(p, TK_LPAREN))
      // This is an argument list.
      fn_var(p, ident_tok.slice);

    else {
      Local *local = create_local_var(p, ident_tok.slice);

      if (match_assignment(p)) {
        expr_rhs(p, PREC_ASSIGN, ASSOC_NONE);
        local->initialized = true;
      }
      else
        emit_byte(p, ident_tok, OP_RESERVE_SLOT);
    }

    p->c->stack_slot_count++;
    decl_count++;

    if (!match(p, TK_COMMA))
      break;
    if (peek_linewise(p).type == TK_IN)
      break; // Trailing comma.
  }

  Local *locals = &p->c->locals.data[locals_idx];
  resolve_var(p, locals, decl_count);

  if (is_statement) {
    if (!match(p, TK_IN)) {
      // Statement style `var`.
      p->c->depth--;
      semantic(p)[-1].statement_count += decl_count - 1;

      // Last slot is already accounted for when parsing statements.
      p->c->stack_slot_count--;

      // Move local declarations into outer statement scope.
      for (size_t i = 0; i < decl_count; i++)
        locals[i].depth--;
      return;
    }
  }
  else consume(p, TK_IN, "expect `in` after `var` expression");

  // Expression style `var`
  expr(p, PREC_TOP);
  p->c->stack_slot_count++;

  clear_local_scope(p);
  end_block(p, p->current, decl_count + 1);
  p->c->depth--;
}

// `as` is alternative syntax for name binding.
// ... as x
static void as_bind(Parse *p, int min_bp)
{
  if (PREC_TOP < min_bp) {
    semantic(p)->led_end = true;
    return;
  }

  Token tok = p->current;

  bool is_statement = semantic(p)->in_stmts;
  size_t decl_count = 0;

  size_t first_local_idx = p->c->locals.len;
  p->c->depth++;

  // Consume declarations
  for (;;) {
    consume(p, TK_AS, "expect `as`");
    Str ident = consume(p, TK_WORD, "expect identifier after `as`").slice;

    create_local_var(p, ident)->initialized = true;
    decl_count++;
    p->c->stack_slot_count++;

    if (!match(p, TK_COMMA))
      break;
    if (peek_linewise(p).type == TK_IN)
      break; // Trailing comma.

    // Parse the next bound expression
    expr_rhs(p, PREC_TOP, ASSOC_LEFT);
  }

  if (is_statement && peek_linewise(p).type != TK_IN) {
    // Statement style `as`.
    p->c->locals.data[first_local_idx].depth--;
    p->c->depth--;

    // Slot is already accounted for.
    p->c->stack_slot_count--;

    // You can only have a single declaration inside an `as` stmt.
    // Why use commas to delimit when you can already have semicolons?
    if (decl_count > 1)
      parse_error(p, tok, true,
          "cannot have multiple declarations in an `as` statement");
    return;
  }

  // Expression style `as`
  consume(p, TK_IN, "expect `in` after `as` expression");
  expr(p, PREC_TOP);
  p->c->stack_slot_count++;

  clear_local_scope(p);
  end_block(p, tok, decl_count + 1);
  p->c->depth--;
}

static inline bool is_else(Parse *p)
{
  Token tok = p->current;

  // Allow `else` on the same indentation level as `if`
  if (tok.type == TK_LINE) {
    if (tok.slice.len >= semantic(p)->indent.initial)
      tok = next(p);
    else
      return false;
  }

  return tok.type == TK_ELSE || tok.type == TK_ELIF;
}

static void if_then(Parse *p)
{
  Token if_tok = eat(p);

  // Parse condition.
  expr(p, PREC_NONE);
  size_t operand_idx = defer_op(p, if_tok, OP_IF);

  consume(p, TK_THEN, "expect `then` after `if`");
  // Parse conditional value.
  expr(p, PREC_IF);

  if (is_else(p)) {
    // When directly chaining with `else`, don't bother creating a Maybe
    change_opcode(p, operand_idx, OP_JMP_WHEN_FALSE);

    semantic(p)->else_chained = true;
    semantic(p)->else_jmp_idx = operand_idx;
  }
  else {
    // Create an optional value.
    emit_byte(p, p->current, OP_MAKE_SOME);
    patch_jump(p, if_tok, operand_idx);
  }
}

// else elif
static void else_clause(Parse *p, int min_bp)
{
  // Left-denoted operator.
  if (PREC_ELSE < min_bp) {
    semantic(p)->led_end = true;
    return;
  }
  Token else_tok = p->current;
  bool is_elif = else_tok.type == TK_ELIF;

  Opcode opcode =
    semantic(p)->else_chained ? OP_JMP
                              : (is_elif ? OP_ELIF : OP_ELSE);
  size_t operand_idx =
    defer_op(p, else_tok, opcode);

  // Patch any jumps into the `else`
  if (semantic(p)->else_chained)
    patch_jump(p, else_tok, semantic(p)->else_jmp_idx);

  if (is_elif) if_then(p);
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

static inline Loop *init_loop(Parse *p, Str label)
{
  Loop loop;
  loop.label = label;

  loop.breaks = JumpIndices_init();
  loop.continues = JumpIndices_init();

  loop.start = code_idx(p);
  loop.stack_slot = p->c->stack_slot_count;

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
        // 2 accounts for the 16-bit operand of the instruction
        loop->iter - continue_idx - 2);
  }

  free(loop->breaks.data);
  free(loop->continues.data);

  LoopStack_pop(&p->c->loops);
}

// loop for while.
static void loop(Parse *p)
{
  // The p-code of a loop is generally aligned as follows:
  // --- HEAD ---
  // (0) Stack slot of the `for` iterable
  // (1) Slot for the result of the loop (initialized as `no` type)
  //
  // --- BODY ---
  // (2) Conditional jump: if condition is false, go to END
  // (3) Loop expression
  //
  // --- ITERATION ---
  // (4) If captured, the loop variable gets hoisted
  // (5) `for` advances its iterable
  // (6) Loop back to BODY
  //
  // --- END ---

  Token tok = eat(p);
  TokenType type = tok.type;

  Str label = loop_label(p);

  Str for_identifier;

  // `for` loop initializer
  if (type == TK_FOR) {
    for_identifier = consume(p, TK_WORD, "expect identifier").slice;
    consume(p, TK_IN, "expect `in`");

    // Iterable value
    expr(p, PREC_NONE);
    p->c->stack_slot_count++;
  }

  // Reserve initial value slot for the result of the last cycle.
  emit_byte(p, tok, OP_RESERVE_SLOT);
  p->c->stack_slot_count++;

  // Loop start!
  Loop *loop = init_loop(p, label);
  loop->is_for = type == TK_FOR;

  // Emit conditional jump
  size_t cond_jmp_idx;
  switch (type) {
  default: unreachable();

  case TK_LOOP:
    // No conditional jump, but discard what the last cycle evaluated to
    cond_jmp_idx = false;
    break;

  case TK_WHILE:
    // `while` has its conditional jump.
    expr(p, PREC_NONE);
    cond_jmp_idx = defer_op(p, tok, OP_JMP_WHEN_FALSE);
    break;

  case TK_FOR:
    // `for` creates a loop variable which is initialized with the next value
    // from the iterable.
    emit_byte(p, tok, OP_FOR);
    create_local_var(p, for_identifier)->initialized = true;
    p->c->stack_slot_count++;

    // If the iterable is finished, termination ensues.
    cond_jmp_idx = defer_op(p, tok, OP_FOR_JMP);
    break;
  }

  // `do` separates the head and the body.
  if (type != TK_LOOP)
    consume(p, TK_DO, "expect `do`");

  // Parse loop body
  expr(p, PREC_TOP);

  if (type == TK_FOR) {
    // The `for` loop variable is created and moved out of scope on each
    // iteration. This ensures that any closures over the variable will get the
    // version of it seen in their loop cycle.
    clear_local(p);
    // Discard the variable's slot.
    end_block(p, tok, 2);
  }

  // Result slot
  p->c->stack_slot_count--;

  loop->iter = code_idx(p);
  emit_loop(p, tok, OP_LOOP, loop->start);

  // Loop end!
  end_loop(p, tok, loop);

  // Land the conditional jump here.
  if (cond_jmp_idx) patch_jump(p, tok, cond_jmp_idx);
}

// Emit the result of a control flow keyword
static void control_flow_result(Parse *p)
{
  bool has_result = p->current.type == TK_LINE
    ? is_continued_line(p, p->current.slice.len) : is_expr(p, p->current);

  if (has_result)
    expr_rhs(p, PREC_FLOW, ASSOC_LEFT); // Parse resulting value.
  else
    emit_byte(p, p->current, OP_RESERVE_SLOT);
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

// break [value]
// continue [value]
static void loop_flow(Parse *p)
{
  Token tok = eat(p);

  Loop *loop = resolve_loop(p, tok);
  control_flow_result(p);

  if (loop == NULL) return;

  size_t stack_slot = loop->stack_slot;
  // Don't discard result slot when `continue`-ing.
  // OP_LOOP needs that!
  if (tok.type == TK_CONTINUE)
    stack_slot++;

  emit_var_op(p, tok, OP_LEVEL_BLOCK, stack_slot);

  JumpIndices *worklist;

  switch (tok.type) {
  default: unreachable();

  case TK_BREAK:
    worklist = &loop->breaks;
    if (loop->is_for)
      emit_byte(p, tok, OP_FOR_DISCARD);
    break;

  case TK_CONTINUE:
    worklist = &loop->continues;
    break;
  }

  size_t jmp_idx = defer_op(p, tok, OP_JMP);
  JumpIndices_push(worklist, jmp_idx);
}

// return [value]
static void returnage(Parse *p)
{
  next(p);
  control_flow_result(p);
  emit_return(p, true);
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

  // We are parenthesized, looking at an identifier followed by a comma.
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
  semantic(p)->led_end = true;
}

static const ParseRule parse_rules[] =
  {
/*  token type           NUD         LED        */
    [TK_EOF]         = { NULL,       led_end     },
    [TK_ERR]         = { NULL,       NULL        },

    [TK_LINE]        = { NULL,       indentation },

    [TK_PLUS]        = { unary_plus, infix_op    },
    [TK_MINUS]       = { prefix_op,  infix_op    },
    [TK_STAR]        = { NULL,       infix_op    },
    [TK_SLASH]       = { NULL,       infix_op    },
    [TK_CARET]       = { NULL,       infix_op    },
    [TK_PERCENT]     = { NULL,       led_op      },
    [TK_BANG]        = { NULL,       postfix_op  },
    [TK_2PIPE]       = { NULL,       infix_op    },

    [TK_EQ]          = { NULL,       cmp         },
    [TK_NEQ]         = { NULL,       cmp         },
    [TK_LT]          = { NULL,       cmp         },
    [TK_GT]          = { NULL,       cmp         },
    [TK_LEQ]         = { NULL,       cmp         },
    [TK_GEQ]         = { NULL,       cmp         },

    [TK_ASSIGN]      = { NULL,       assign      },

    [TK_VAR]         = { var_bind,   NULL        },
    [TK_AS]          = { NULL,       as_bind     },

    [TK_IN]          = { NULL,       led_end     },

    [TK_NOT]         = { prefix_op,  NULL        },
    [TK_AND]         = { NULL,       infix_op    },
    [TK_OR]          = { NULL,       infix_op    },

    [TK_MOD]         = { NULL,       infix_op    },

    [TK_IF]          = { if_then,    NULL        },
    [TK_THEN]        = { NULL,       led_end     },
    [TK_ELSE]        = { NULL,       else_clause },
    [TK_ELIF]        = { NULL,       else_clause },

    [TK_LOOP]        = { loop,       NULL        },
    [TK_FOR]         = { loop,       NULL        },
    [TK_WHILE]       = { loop,       NULL        },
    [TK_DO]          = { NULL,       led_end     },

    [TK_BREAK]       = { loop_flow,  NULL        },
    [TK_CONTINUE]    = { loop_flow,  NULL        },
    [TK_RETURN]      = { returnage,  NULL        },

    [TK_TRUE]        = { boolean,    NULL        },
    [TK_FALSE]       = { boolean,    NULL        },

    [TK_SOME]        = { some,       NULL        },
    [TK_NONE]        = { none,       NULL        },

    [TK_ARROW]       = { NULL,       infix_op    },
    [TK_MAPS_TO]     = { NULL,       NULL        },

    [TK_LPAREN]      = { parens,     invocation  },
    [TK_RPAREN]      = { NULL,       led_end     },

    [TK_LBRACK]      = { list,       subscript   },
    [TK_AT_LBRACK]   = { table,      NULL        },
    [TK_RBRACK]      = { NULL,       led_end     },

    [TK_LCURLY]      = { code_block, NULL        },
    [TK_RCURLY]      = { NULL,       led_end     },

    [TK_COLON]       = { NULL,       ufcs        },
    [TK_SEMICOLON]   = { NULL,       led_end     },
    [TK_COMMA]       = { NULL,       led_end     },

    [TK_DOT]         = { NULL,       subscript   },
    [TK_DOTDOT]      = { NULL,       NULL        },

    [TK_Q_DOT]       = { NULL,       q_subscript },
    [TK_Q_LBRACK]    = { NULL,       q_subscript },
    [TK_INTERROBANG] = { NULL,       interrobang },

    [TK_NUMERAL]     = { number,     NULL        },

    [TK_STRCONT]     = { metastring, NULL        },
    [TK_STREND]      = { string,     NULL        },

    [TK_WORD]        = { identifier, NULL        },
    [TK_LABEL]       = { NULL,       led_end     },
 };

static const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

Procedure *compile(Varmint *vm, Parse *p, bool discard_state, String *source)
{
  bool ad_hoc = p == NULL;

  Parse new_parse;
  size_t initial_slot_count;

  if (ad_hoc) {
    // Embark on a brand new parse.
    new_parse = init_parse(vm);
    p = &new_parse;
  }
  else
    initial_slot_count = p->c->stack_slot_count;

  init_new_code(p, source);

  // Parse program.
  if (p->current.type == TK_EOF) {
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
    discard_state = true;
  }

  if (!ad_hoc && discard_state) {
    // Reset parse state for the next run.
    p->had_error = semantic(p)->panic = false;

    // Delete top level locals
    while (Locals_top(&p->c->locals)->stack_slot >= initial_slot_count)
      Locals_pop(&p->c->locals);

    // Ignore any tallied slots
    p->c->stack_slot_count = initial_slot_count;
  }

  return procedure;
}
