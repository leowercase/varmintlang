#include "error.h"
#include "lex.h"
#include "compile.h"
#include "val.h"
#include "stdarg.h"

#include <stdio.h>

static inline PCode *code(Parse *p)
{
  return &p->c->procedure->code;
}

static inline SemanticDatum *semantic(Parse *p)
{
  return SemanticData_top(&p->semantic);
}

static inline SemanticDatum *new_semantic_scope(Parse *p)
{
  SemanticDatum sem;
  sem.assign_fn = semantic(p)->assign_fn;
  sem.led_fail = false;
  sem.panic = false;

  return SemanticData_push(&p->semantic, sem);
}

static inline SemanticDatum end_semantic_scope(Parse *p)
{
  return SemanticData_pop(&p->semantic);
}

static void init_compiler(Parse *p, Locals args)
{
  Compiler *c = allocate(NULL, sizeof(Compiler));
  c->depth = 0;
  c->procedure = proc_new((int)args.len);
  c->locals = args;
  c->stack_slot_count = args.len;

  // Switch compilers.
  // We're one function nesting level deeper.
  c->enclosing = p->c;
  p->c = c;
}

// Return from compiler.
static Proc *return_compiler(Parse *p)
{
  Proc *procedure = p->c->procedure;

  // Return from procedure.
  emit_byte(code(p), p->current.line, OP_RETURN);

  free(p->c->locals.data);

  p->c = p->c->enclosing;
  return procedure;
}

// Issue a parsing error and enter panic mode in the imminent semantic scope.
static void parse_error(Parse *p, Token offending_tok, const char *msg, ...)
{
  if (semantic(p)->panic) return;

  error_out("[line %li] ", offending_tok.line);

  va_list args;
  va_start(args, msg);
  v_error_out(msg, args);
  va_end(args);
  error_out(":\n");

  error_line_snip(p->vm->source, offending_tok.line,
                         (char *)offending_tok.slice.s);
  p->had_error = true;
  semantic(p)->panic = true;
}

// Emit a code constant.
static Value *emit_constant(Parse *p, size_t line, Value value)
{
  Value *constant = Constants_push(&code(p)->constants, value);
  size_t idx = code(p)->constants.len - 1;

  if (!emit_size_op(code(p), line, OP_CONST, idx))
    parse_error(p, p->current, "too many constants");

  return constant;
}

// Local lookup.
static Local *resolve_local(Parse *p, StrSlice name)
{
  if (p->c->locals.len == 0)
    return NULL;

  for (Local *local = Locals_top(&p->c->locals);
      local >= p->c->locals.data;
      local--)
    if (strs_eq(local->name, name))
      return local;

  return NULL;
}

static Local *create_local(Locals *locals,
    StrSlice name, int depth, bool initialized)
{
  Local local = {name, depth, initialized, 0};
  return Locals_push(locals, local);
}

static Local *create_local_var(Parse *p, StrSlice name)
{
  return create_local(&p->c->locals,
                      name, p->c->depth, false);
}

static void patch_local(Parse *p, Local *local)
{
  // Because variables are declared in a stack-like manner,
  // we can predict the op stack slot they will occupy
  local->stack_slot = p->c->stack_slot_count;
}

static void clear_local_scope(Parse *p)
{
  if (p->c->locals.len == 0)
    return;

  for (Local *local = Locals_top(&p->c->locals);
      local >= p->c->locals.data && local->depth == p->c->depth;
      local--)
    Locals_pop(&p->c->locals);
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
    Token t = p->lookahead = lex_token(&p->lex);

    // Catch as many consecutive error tokens as possible.
    if (t.type != TK_ERR) break;
    parse_error(p, t, "%.*s", (int)t.slice.len, t.slice.s);
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

static Token consume(Parse *p, TokenType expected, const char *msg)
{
  Token tok = p->current;
  if (!match(p, expected))
    parse_error(p, p->current, msg);
  return tok;
}

static const ParseRule *parse_rule(TokenType t);
static void expr(Parse *p, int min_bp);

typedef struct {
  Op type;
  Precedence precedence;
} UnaryOp;

typedef struct {
  Op type;
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
  emit_byte(code(p), op_tok.line, (uint8_t)op.type);
}

// Allows compound assignment shorthand +:=
// https://en.cppreference.com/w/c/language/operator_assignment.html#Compound_assignment
static inline void assignage(Parse *p, int min_bp, Op op_shorthand)
{
  if (PREC_ASSIGN < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  size_t line = eat(p).line; // op
  bool compound = op_shorthand != OP_NONE;

  if (compound)
    next(p); // :=
    // Compound lhs has already been emitted.

  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  expr(p, r_bp); // Parse and emit rhs.

  if (compound)
    // Emit compound opcode
    emit_byte(code(p), line, (uint8_t)op_shorthand);

  // Emit assigning instruction.
  if (semantic(p)->assign_fn != NULL)
    semantic(p)->assign_fn(p);
  else
    parse_error(p, p->current, "lhs is not assignable");
}

static void assign(Parse *p, int min_bp)
{
  assignage(p, min_bp, OP_NONE);
}

static const BinaryOp infix_ops[] = {
  [TK_PLUS]    = { OP_ADD,     PREC_TERM,   ASSOC_LEFT  },
  [TK_MINUS]   = { OP_SUB,     PREC_TERM,   ASSOC_LEFT  },
  [TK_STAR]    = { OP_MUL,     PREC_FACTOR, ASSOC_LEFT  },
  [TK_SLASH]   = { OP_DIV,     PREC_FACTOR, ASSOC_LEFT  },
  [TK_CARET]   = { OP_POW,     PREC_POWER,  ASSOC_RIGHT },
  [TK_PERCENT] = { OP_MODULO,  PREC_FACTOR, ASSOC_LEFT  },
  [TK_2PIPE]   = { OP_CONCAT,  PREC_CONCAT, ASSOC_LEFT  },

  [TK_EQ]      = { OP_EQ,      PREC_CMP,    ASSOC_LEFT  },
  [TK_NEQ]     = { OP_NEQ,     PREC_CMP,    ASSOC_LEFT  },
  [TK_LT]      = { OP_LT,      PREC_CMP,    ASSOC_LEFT  },
  [TK_LEQ]     = { OP_LEQ,     PREC_CMP,    ASSOC_LEFT  },
  [TK_GT]      = { OP_GT,      PREC_CMP,    ASSOC_LEFT  },
  [TK_GEQ]     = { OP_GEQ,     PREC_CMP,    ASSOC_LEFT  },

  [TK_AND]     = { OP_AND,     PREC_AND,    ASSOC_LEFT  },
  [TK_OR]      = { OP_OR,      PREC_OR,     ASSOC_LEFT  },
  [TK_IN]      = { OP_IN,      PREC_IN,     ASSOC_LEFT  },
  [TK_NOTIN]   = { OP_NOTIN,   PREC_IN,     ASSOC_LEFT  },
  [TK_MOD]     = { OP_MODULO,  PREC_FACTOR, ASSOC_LEFT  },
  [TK_ARROW]   = { OP_I9N,     PREC_I9N,    ASSOC_LEFT  },
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

  emit_byte(code(p), op_token.line, (uint8_t)op.type);
}

static inline bool is_prefix_and_infix(TokenType op)
{
  // Can be extended later.
  return op == TK_PLUS || op == TK_MINUS;
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT] = { OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]    = { OP_FACTORIAL,  PREC_FACTORIAL },
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
  emit_byte(code(p), op_token.line, (uint8_t)op.type);
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

static void assign_list(Parse *p)
{
  emit_byte(code(p), p->current.line, OP_LIST_SET);
}

// list[i]
static void subscript(Parse *p, int min_bp)
{
  if (PREC_CALL < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  Token brack_tok = eat(p); // [
  expr(p, PREC_NONE);
  consume(p, TK_RBRACK, "unterminated subscript"); // ]

  if (p->current.type != TK_ASSIGN)
    // Access.
    emit_byte(code(p), brack_tok.line, OP_LIST_GET);

  else if (semantic(p)->assign_fn == NULL)
    parse_error(p, brack_tok, "invalid list assign");

  semantic(p)->assign_fn = assign_list;
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
static Locals consume_arg_list(Parse *p)
{
  Locals args = Locals_new();

  // Parameters take up the first few stack slots of the frame.
  while (p->current.type == TK_WORD
      && (peek(p).type == TK_RPAREN || peek(p).type == TK_COMMA)) {
    size_t arg_slot = args.len;
    // Create argument local.
    create_local(&args, eat(p).slice, 0, true)
      ->stack_slot = arg_slot;

    if (!match(p, TK_COMMA)) break;
  }

  return args;
}

// Parses a function body and creates a new fn.
static void function(Parse *p, size_t line, StrSlice name, Locals args)
{
  Value *fn_constant = emit_constant(p, line, NO_VAL);

  init_compiler(p, args);
  expr(p, PREC_NONE);

  Proc *procedure = return_compiler(p);
  procedure->name = name;
  // Functions are values, too!
  *fn_constant = value_new(procedure, function);
}

static void ident_str(Parse *p, Token ident_tok);

// (x, y) => ...
// https://en.wikipedia.org/wiki/Maps_to
static void maplet(Parse *p)
{
  size_t line = p->current.line;
  Locals args = consume_arg_list(p);

  if (peek(p).type != TK_MAPS_TO) {
    // Doesn't match a maplet.

    if (args.len == 0)
      // Need to parse expr inside grouping.
      expr(p, PREC_NONE);

    else if (args.len == 1) {
      // Consumed a single identifier in paretheses - that needs to be emitted.
      // We already pushed it into locals in consume_arg_list
      Token ident_tok = {.line = p->current.line,
                         .slice = Locals_pop(&args).name};
      ident_str(p, ident_tok);
    }

    else
      // Something is wrong in the user's code.
      parse_error(p, p->current,
          "expect maplet arrow after argument list");

    free(args.data);
    consume(p, TK_RPAREN, "expect grouping end");
    return;
  }

  consume(p, TK_RPAREN, "expect argument list end");
  next(p); // =>
  function(p, line, NULL_STR, args);
}

// (...)
static void grouping(Parse *p)
{
  if (consume_arg_list_start(p)) // (
    maplet(p);

  else {
    expr(p, PREC_NONE);
    consume(p, TK_RPAREN, "expect grouping end"); // )
  }
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
        parse_error(p, p->current,
            "invalid trailing %s in listing", tok_cstring(delim));
    }

    expr(p, PREC_NONE);
    len++;

    if (match(p, end)) // ]
      return len;
  } while (match(p, delim)); // ,

  parse_error(p, p->current,
      "expect %s in listing", tok_cstring(delim));
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
  size_t line = p->current.line;

  size_t arity = delimited_listing(p,
      TK_LPAREN, TK_COMMA, TK_RPAREN, false); // Parse argument list.
  emit_size_op(code(p), line, OP_CALL, arity);
}

// [a, b, c]
static void list(Parse *p)
{
  size_t line = p->current.line;

  size_t list_len = delimited_listing(p, TK_LBRACK, TK_COMMA, TK_RBRACK, true);
  emit_size_op(code(p), line, OP_BUILD_LIST, list_len);
}

static void construct_body(Parse *p)
{
  consume(p, TK_COLON, "expect `:`");
  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  expr(p, r_bp);
}

// if while.
// These are in essence mixfix operators terminated by :
static void cond(Parse *p)
{
  //Token cond_tok = eat(p);
  abort();
}

// elif is likewise mixfix, else on the other hand binary.
static void else_elif(Parse *p, int min_bp)
{
  if (PREC_BASE < min_bp) {
    semantic(p)->led_fail = true;
    return;
  }

  //Token tok = eat(p);
  abort();
}

static void loop(Parse *p)
{
  size_t line = eat(p).line; // loop
  uint8_t *ip = defer_op(code(p), line, OP_JMP);

  construct_body(p);

}

static void for_loop(Parse *p)
{
  //Token for_tok = eat(p),
        //ident_tok = consume(p, TK_WORD);
  //consume(p, TK_IN);

  // TODO
  abort();

  const int r_bp = (int)PREC_BASE + (int)ASSOC_RIGHT;
  expr(p, r_bp);
  construct_body(p);
}

// break [... break] [continue]
static void loop_break(Parse *p)
{
  //int breaks;
  //for (breaks = 0; match(p, TK_BREAK); breaks++);
  //bool continues = match(p, TK_CONTINUE);

  // TODO
  abort();
}

// continue
static void loop_cont(Parse *p)
{
  // TODO
  abort();
}

// using f, g, h: ...
static void using(Parse *p)
{
  size_t line = eat(p).line;
  p->c->depth++;

  size_t native_count = 0;
  do {
    Token ident_tok = consume(p, TK_WORD,
        "expect native function name after `using`");
    StrSlice name = ident_tok.slice;

    Local *local = create_local_var(p, name);
    local->initialized = true;

    NativeFn *native_fn =
      NativesTable_get(&p->vm->natives, name);

    if (native_fn == NULL)
      parse_error(p, ident_tok,
          "no native function named %.*s", (int)name.len, name.s);

    emit_constant(p, ident_tok.line, value_new(native_fn, native));
    patch_local(p, local);

    // Native fn locals occupy space too, you know.
    native_count++;
    p->c->stack_slot_count++;
  } while (match(p, TK_COMMA));

  construct_body(p);

  clear_local_scope(p);
  emit_size_op(code(p), line, OP_RETAIN1_DISCARDN, native_count + 1);
  p->c->depth--;
}

static void boolean(Parse *p)
{
  Token booltok = eat(p);
  int P = booltok.type == TK_TRUE;
  emit_constant(p, booltok.line, value_new(P, boolean));
}

static void number(Parse *p)
{
  Token ntok = eat(p);

  // Copying the slice to NUL-terminated so strtod doesn't parse anything extra
  Str nstr = str_copy(ntok.slice);
  float64_t n = strtod(nstr.s, NULL);
  free((void *)nstr.s);

  emit_constant(p, ntok.line, value_new(n, number));
}

static void string(Parse *p)
{
  Token strtok = eat(p);
  Str str = str_copy(strtok.slice);

  emit_constant(p, strtok.line, string_value_new(str));
}

static void metastring(Parse *p)
{
  size_t metas = 0;
  size_t line = p->current.line;

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
    default:
      expr(p, PREC_NONE); // \(...)
    }
  }

  emit_size_op(code(p), line, OP_BUILD_STR, metas);
}

static void assign_local(Parse *p)
{
  Local *local = semantic(p)->assignable_local;
  emit_size_op(code(p), p->current.line, OP_SET, local->stack_slot);
  local->initialized = true;
}

// Emit p-code based on an identifier occurrence.
static void ident_str(Parse *p, Token ident_tok)
{
  StrSlice ident = ident_tok.slice;

  Local *local = resolve_local(p, ident);
  if (!local) {
    parse_error(p, ident_tok, "use of undeclared variable %.*s",
        (int)ident.len, ident.s);
    return;
  }

  semantic(p)->assign_fn = assign_local;
  semantic(p)->assignable_local = local;

  if (p->current.type != TK_ASSIGN) {
    // Access.
    if (local->initialized)
      emit_size_op(code(p), ident_tok.line, OP_GET, local->stack_slot);
    else
      parse_error(p, ident_tok, "variable %.*s has not been initialized",
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
static void fn_decl(Parse *p, Token ident_tok)
{
  if (!consume_arg_list_start(p))
    parse_error(p, p->current, "expect argument list");

  StrSlice name = ident_tok.slice;
  Locals args = consume_arg_list(p);
  consume(p, TK_RPAREN, "expect argument list end");

  consume(p, TK_ASSIGN, "function requires a body");

  Local *fn_local = create_local_var(p, name);
  fn_local->initialized = true;

  patch_local(p, fn_local);
  function(p, p->current.line, name, args);
}

// let ...
static void let(Parse *p)
{
  new_semantic_scope(p);

  next(p); // let token
  Token ident_tok = consume(p, TK_WORD,
      "expect identifier after `let`");

  if (p->current.type == TK_LPAREN) {
    // Argument list for function definition.
    fn_decl(p, ident_tok);
    return;
  }

  Local *local = create_local_var(p, ident_tok.slice);

  if (match(p, TK_ASSIGN)) {
    const int assign_r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;

    expr(p, assign_r_bp);
    patch_local(p, local);

    local->initialized = true;
  }
  else {
    emit_byte(code(p), p->current.line, OP_RESERVE_SLOT);
    patch_local(p, local);
  }

  end_semantic_scope(p);
}

static void stmt(Parse *p)
{
  if (p->current.type == TK_LET)
    // let is a statement, as it requires stack semantics.
    let(p);
  else
    expr(p, PREC_NONE);
}

// A block is a series of statements.
static void block(Parse *p)
{
  Token curly_tok = eat(p); // {

  if (p->current.type == TK_RCURLY) {
    parse_error(p, eat(p), "illegal empty block");
    return;
  }

  new_semantic_scope(p);
  p->c->depth++;

  // Consume first statement
  stmt(p);
  size_t stmts = 1;
  p->c->stack_slot_count++;

  // Consume statements ...;
  for (; !match(p, TK_RCURLY); stmts++, p->c->stack_slot_count++) {
    // Consume tokens until a semicolon is found.
    while (!match(p, TK_SEMICOLON))
      parse_error(p, eat(p), "expect semicolon");

    semantic(p)->panic = false; // Synchronize error state between statements.

    if (match(p, TK_RCURLY)) {
      // Trailing semicolon, no value from block expr.
      emit_byte(code(p), curly_tok.line, OP_RESERVE_SLOT);
      stmts++;
      p->c->stack_slot_count++;
      break;
    }

    if (match(p, TK_RCURLY)) break;

    stmt(p);
  }

  clear_local_scope(p);
  emit_size_op(code(p), p->current.line, OP_RETAIN1_DISCARDN, stmts);

  p->c->depth--;
  end_semantic_scope(p);
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
static void expr(Parse *p, int min_bp)
{
  new_semantic_scope(p);

  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL)
    parse_error(p, lhs_token, "expect expression, got `%.*s`",
        (int)lhs_token.slice.len, lhs_token.slice.s);
  else
    lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      parse_error(p, op_token, "expect operator, got `%.*s`",
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
    [TK_EQ]        = { NULL,       infix_op   },
    [TK_NEQ]       = { NULL,       infix_op   },
    [TK_LT]        = { NULL,       infix_op   },
    [TK_GT]        = { NULL,       infix_op   },
    [TK_LEQ]       = { NULL,       infix_op   },
    [TK_GEQ]       = { NULL,       infix_op   },

    [TK_ASSIGN]    = { NULL,       assign     },
    [TK_LET]       = { NULL,       NULL       },

    [TK_NOT]       = { prefix_op,  NULL       },
    [TK_AND]       = { NULL,       infix_op   },
    [TK_OR]        = { NULL,       infix_op   },
    [TK_IN]        = { NULL,       infix_op   },
    [TK_NOTIN]     = { NULL,       infix_op   },

    [TK_MOD]       = { NULL,       infix_op   },

    [TK_IF]        = { cond,       NULL       },
    [TK_ELSE]      = { NULL,       else_elif  },
    [TK_ELIF]      = { NULL,       else_elif  },

    [TK_LOOP]      = { loop,       NULL       },
    [TK_FOR]       = { for_loop,   NULL       },
    [TK_WHILE]     = { cond,       NULL       },

    [TK_BREAK]     = { loop_break, NULL       },
    [TK_CONTINUE]  = { loop_cont,  NULL       },

    [TK_USING]     = { using,      NULL       },

    [TK_TRUE]      = { boolean,    NULL       },
    [TK_FALSE]     = { boolean,    NULL       },

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

    [TK_NUMERAL]   = { number,     NULL       },

    [TK_STRCONT]   = { metastring, led_end    },
    [TK_STREND]    = { string,     led_end    },

    [TK_WORD]      = { ident,      NULL       },
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
  next(&p); next(&p);

  p.had_error = false;
  p.semantic = SemanticData_new();

  p.c = NULL;
  init_compiler(&p, Locals_new());
  return p;
}

Proc *compile(Varmint *vm, char *source)
{
  Parse p = init_parse(vm, source);

  if (p.current.type == TK_EOF) {
    error_out("empty file\n");
    return NULL;
  }

  SemanticDatum sem;
  sem.assign_fn = NULL;
  sem.led_fail = false;
  sem.panic = false;
  SemanticData_push(&p.semantic, sem);

  expr(&p, PREC_NONE);

  end_semantic_scope(&p);

  Proc *procedure = return_compiler(&p);
  return p.had_error ? NULL : procedure;
}
