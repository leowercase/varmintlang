#include "lex.h"
#include "parsing.h"
#include "val.h"

static inline Token peek(Parser *p)
{
  return p->lookahead;
}

static inline Token next(Parser *p)
{
  Token next_tok = p->lookahead;
  p->current = next_tok;
  p->lookahead = lex_token(&p->lex);
  return next_tok;
}

static inline Token eat(Parser *p)
{
  Token tok = p->current;
  next(p);
  return tok;
}

static bool match(Parser *p, TokenType expected)
{
  Token tok = p->current;
  if (tok.type != TK_EOF && tok.type == expected) {
    next(p);
    return true;
  }
  else return false;
}

static Token consume(Parser *p, TokenType expected)
{
  Token tok = p->current;
  if (tok.type != TK_EOF && tok.type == expected) {
    next(p);
    return tok;
  }
  else invalid_token(tok);

  abort(); // Unreachable.
}

static const ParseRule parse_rules[] =
  {
/*  token type       NUD         LED        */
    [TK_EOF]     = { NULL,       no_op      },
    [TK_ERR]     = { NULL,       NULL       },

    [TK_PLUS]    = { prefix_op,  infix_op   },
    [TK_MINUS]   = { prefix_op,  infix_op   },
    [TK_STAR]    = { NULL,       infix_op   },
    [TK_SLASH]   = { NULL,       infix_op   },
    [TK_CARET]   = { NULL,       infix_op   },
    [TK_PERCENT] = { NULL,       led_op     },
    [TK_BANG]    = { NULL,       postfix_op },
    [TK_2PIPE]   = { NULL,       infix_op   },

    [TK_EQ]      = { NULL,       cmp_op     },
    [TK_NEQ]     = { NULL,       cmp_op     },
    [TK_LT]      = { NULL,       cmp_op     },
    [TK_GT]      = { NULL,       cmp_op     },
    [TK_LEQ]     = { NULL,       cmp_op     },
    [TK_GEQ]     = { NULL,       cmp_op     },

    [TK_ASSIGN]  = { NULL,       NULL       },
    [TK_LET]     = { NULL,       NULL       },

    [TK_NOT]     = { prefix_op,  NULL       },
    [TK_AND]     = { NULL,       infix_op   },
    [TK_OR]      = { NULL,       infix_op   },

    [TK_TRUE]    = { boolean,    NULL       },
    [TK_FALSE]   = { boolean,    NULL       },

    [TK_ARROW]   = { NULL,       infix_op   },

    [TK_LPAREN]  = { grouping,   NULL       },
    [TK_RPAREN]  = { NULL,       no_op      },

    [TK_LCURLY]  = { block,      NULL       },
    [TK_RCURLY]  = { NULL,       no_op      },

    [TK_SEMICOL] = { NULL,       no_op      },
    [TK_COMMA]   = { NULL,       list       },

    [TK_NUMERAL] = { number,     NULL       },

    [TK_STRCONT] = { metastring, no_op      },
    [TK_STREND]  = { string,     no_op      },

    [TK_WORD]    = { ident,      NULL       },
  };

static inline
const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

typedef struct {
  Opcode opcode;
  Precedence precedence;
} UnaryOp;

typedef struct {
  Opcode opcode;
  Precedence precedence;
  Associativity associativity;
} BinaryOp;

static const UnaryOp prefix_ops[] = {
    [TK_PLUS]  = { /* special case */ OP_NONE, PREC_SIGN },
    [TK_MINUS] = { OP_NEGATE,                  PREC_SIGN },
    [TK_NOT] =   { OP_NOT,                     PREC_NOT  },
};

void prefix_op(Parser *p)
{
  Token op_token = eat(p);
  UnaryOp op = prefix_ops[op_token.type];

  int r_bp = op.precedence;
  expr(p, r_bp); // Parse and emit right operand.

  // A little optimization; unary + does nothing.
  if (op_token.type != TK_PLUS)
    emit_byte(&p->code, op_token.line, op.opcode);
}

static const BinaryOp infix_ops[] = {
  [TK_PLUS]    = { OP_ADD,    PREC_TERM,   ASSOC_LEFT  },
  [TK_MINUS]   = { OP_SUB,    PREC_TERM,   ASSOC_LEFT  },
  [TK_STAR]    = { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
  [TK_SLASH]   = { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
  [TK_CARET]   = { OP_POW,    PREC_POWER,  ASSOC_RIGHT },
  [TK_PERCENT] = { OP_MODULO, PREC_FACTOR, ASSOC_LEFT  },
  [TK_2PIPE]   = { OP_CONCAT, PREC_CONCAT, ASSOC_LEFT  },
  [TK_AND]     = { OP_AND,    PREC_AND,    ASSOC_LEFT  },
  [TK_OR]      = { OP_OR,     PREC_OR,     ASSOC_LEFT  },
  [TK_ARROW]   = { OP_I9N,    PREC_I9N,    ASSOC_LEFT  },
};

bool infix_op(Parser *p, int min_bp)
{
  Token op_token = p->current;
  BinaryOp op = infix_ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  next(p); // Consume op_token

  int r_bp = l_bp + op.associativity;
  expr(p, r_bp); // Parse and emit right operand.

  emit_byte(&p->code, op_token.line, op.opcode);
  return false;
}

static inline bool is_prefix_and_infix(TokenType op)
{
  // Can add more later.
  return op == TK_PLUS || op == TK_MINUS;
}

static const UnaryOp postfix_ops[] = {
  [TK_PERCENT] = { OP_PERCENTAGE, PREC_PERCENT   },
  [TK_BANG]    = { OP_FACTORIAL,  PREC_FACTORIAL },
};

bool postfix_op(Parser *p, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  next(p); // Consume op_token

  emit_byte(&p->code, op_token.line, op.opcode);
  return false;
}

static bool led_op_is_infix(TokenType op, TokenType next)
{
  const ParseRule *next_rule = parse_rule(next);

  if (next_rule->nud == NULL)
    return false; // Next token is not a valid rhs.

  else if (next_rule->led == NULL || !is_prefix_and_infix(next))
    return true; // Next token is not an infix op.

  else {
    int op_bp = infix_ops[op].precedence + infix_ops[op].associativity;
    int next_bp = infix_ops[next].precedence + infix_ops[next].associativity;

    return op_bp < next_bp; // Battle of the binding powers!
  }
}

// LED op tokens of ambiguous fixity.
// 50% + 3
bool led_op(Parser *p, int min_bp)
{
  if (led_op_is_infix(p->current.type, peek(p).type))
    return infix_op(p, min_bp);
  else
    return postfix_op(p, min_bp);
}

// Comparison operators that can be chained.
// a < b <= c != 0
bool cmp_op(Parser *p, int min_bp)
{
  if (PREC_CMP < min_bp)
    return true;

  Token op_token = eat(p);
  size_t line = op_token.line;

  const Opcode opcodes[] = {
    [TK_EQ]  = OP_EQ,
    [TK_NEQ] = OP_NEQ,
    [TK_LT]  = OP_LT,
    [TK_LEQ] = OP_LEQ,
    [TK_GT]  = OP_GT,
    [TK_GEQ] = OP_GEQ,
  };
  Opcode opcode = opcodes[op_token.type];

  const int r_bp = PREC_CMP + ASSOC_LEFT;
  expr(p, r_bp); // Parse and emit right operand.

  // Allow chaining.
  if (is_cmp_token(p->current.type)) {
    // Previous op's rhs becomes next op's lhs!
    emit_byte(&p->code, line, OP_CHAIN_BINOP);

    emit_byte(&p->code, line, opcode);

    cmp_op(p, 0); // Parse and emit chaining operator.

    // 1 = 2 = 3
    // 1 = 2 AND 2 = 3
    emit_byte(&p->code, line, OP_AND);
  }

  else emit_byte(&p->code, line, opcode);

  return false;
}

void grouping(Parser *p)
{
  next(p); // (

  // Parse (...)
  expr(p, PREC_NONE);

  if (!match(p, TK_RPAREN)) // )
    invalid_token(p->current);
}

static void declaration(Parser *p)
{
  next(p); // let

  Token ident_tok = consume(p, TK_WORD);

  size_t stack_slot =
    p->code.constants.len == 0 ? 0 : p->code.constants.len;

  bool initialized;
  if (match(p, TK_ASSIGN)) {
    const int r_bp = PREC_ASSIGN + ASSOC_LEFT;
    expr(p, r_bp);
    initialized = true;
  }
  else {
    emit_constant(&p->code, ident_tok.line, NO_VAL);
    initialized = false;
  }

  // Declare local variable.
  Local local = {ident_tok.slice, p->scope.depth, initialized, stack_slot};
  Locals_push(&p->scope.locals, local);

  if (stack_slot > MAX_OPERAND_SIZE)
    runtime_error("Too many locals!");
}

void stmt(Parser *p)
{
  const int r_bp = PREC_STATEMENT + ASSOC_RIGHT;
  Token tok = p->current;

  // A block is the only place where `let` is allowed.
  // It is the only pure statement in the language's grammar.
  if (tok.type == TK_LET)
    declaration(p);

  else if (parse_rule(p->current.type)->nud != NULL)
    expr(p, r_bp);
  else
    emit_constant(&p->code, tok.line, NO_VAL);
}

// A block is a series of statements.
void block(Parser *p)
{
  next(p); // {

  p->scope.depth++;

  size_t line = p->current.line;
  // Consume first statement
  stmt(p);

  // Consume statements ...;
  int statements = 1;
  for (; match(p, TK_SEMICOL); statements++)
    stmt(p);

  p->scope.depth--;
  Locals *locals = &p->scope.locals;
  while (locals->len > 0 && Locals_top(locals).depth > p->scope.depth)
    Locals_pop(locals);

  // The statement separator ; discards the preceding expression.
  emit_size_op(&p->code, line, OP_RETAIN1_DISCARDN, statements);

  if (!match(p, TK_RCURLY))
    invalid_token(p->current);
}

bool list(Parser *p, int min_bp)
{
  if (PREC_LIST < min_bp)
    return true;

  next(p); // ,

  const int r_bp = PREC_LIST + ASSOC_RIGHT;

  // NB! One list element has already been consumed
  size_t list_len;
  for (list_len = 1; match(p, TK_COMMA); list_len++) {
    if (parse_rule(p->current.type)->led == no_op)
      break;
    expr(p, r_bp);
  }
  printf("List has len %li\n", list_len);

  emit_byte(&p->code, p->current.line, OP_BUILD_LIST);
  error_out("TODO!\n");
  abort();

  return true;
}

void boolean(Parser *p)
{
  Token tok = eat(p);
  bool P;
  switch (tok.type) {
  case TK_TRUE: P = true; break;
  case TK_FALSE: P = false; break;
  default: abort(); // Unreachable
  }
  emit_constant(&p->code, tok.line, value_new((int)P, boolean));
}

void number(Parser *p)
{
  Token tok = eat(p);
  Str n_str = str_copy_slice(tok.slice);
  float64_t n = strtod(n_str.s, NULL);
  emit_constant(&p->code, tok.line, value_new(n, number));
}

void metastring(Parser *p)
{
  string(p); // Consume STRCONT
  size_t substrs = 1;

  for (bool found_end = false; !found_end; substrs++) {
    switch (p->current.type) {
    case TK_STRCONT:
      string(p);
      break;
    case TK_STREND:
      string(p);
      found_end = true;
      break;
    default:
      expr(p, PREC_NONE); // \(...)
    }
  }

  emit_size_op(&p->code, p->current.line, OP_BUILD_STR, substrs);
}

void string(Parser *p)
{
  Token tok = eat(p);
  Str str = str_copy_slice(tok.slice);
  emit_constant(&p->code, tok.line, value_new(str, string));
}

static Local *resolve_local(Parser *p, StrSlice name)
{
  if (p->scope.locals.len == 0)
    return NULL;

  for (size_t i = p->scope.locals.len - 1; i >= 0; i--) {
    Local *local = &p->scope.locals.data[i];

    if (strs_eq(name, local->name))
      return local;
  }

  return NULL;
}

// x
void ident(Parser *p)
{
  Token ident_tok = eat(p);
  StrSlice name = ident_tok.slice;

  Local *local = resolve_local(p, name);
  if (local == NULL) {
    error_out("Expecting valid identifier.\n");
    invalid_token(ident_tok);
  }

  if (match(p, TK_ASSIGN)) {
    // Assignment!
    const int r_bp = TK_ASSIGN + ASSOC_RIGHT;
    expr(p, r_bp);
    emit_size_op(&p->code, ident_tok.line, OP_SET, local->stack_slot);
    local->initialized = true;
  }
  else if (local->initialized)
    // Access!
    emit_size_op(&p->code, ident_tok.line, OP_GET, local->stack_slot);
  else
    // Whoopsie
    runtime_error("Invalid access: %.*s is unitialized",
        (int)name.len, name.s);
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
void expr(Parser *p, int min_bp)
{
  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL) {
    error_out("Expecting expression.\n");
    invalid_token(lhs_token);
  }

  lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      error_out("Expecting operator.\n");
      invalid_token(op_token);
    }

    bool is_no_op = op_rule(p, min_bp);
    if (is_no_op)
      break; // Precedence is too small or op otherwise cannot be used as a LED
  }
}
