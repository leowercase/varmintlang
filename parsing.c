#include "compiler.h"
#include "lex.h"
#include "parsing.h"
#include "val.h"

static inline Token peek(Compiler *c)
{
  return c->lookahead;
}

static inline Token next(Compiler *c)
{
  Token next_tok = c->lookahead;
  c->current = next_tok;
  c->lookahead = lex_token(&c->lex);
  return next_tok;
}

static inline Token eat(Compiler *c)
{
  Token tok = c->current;
  next(c);
  return tok;
}

static bool match(Compiler *c, TokenType expected)
{
  Token tok = c->current;
  if (tok.type != TK_EOF && tok.type == expected)
  {
    next(c);
    return true;
  }
  else return false;
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

    [TK_NOT]     = { prefix_op,  NULL       },
    [TK_AND]     = { NULL,       infix_op   },
    [TK_OR]      = { NULL,       infix_op   },

    [TK_TRUE]    = { boolean,    NULL       },
    [TK_FALSE]   = { boolean,    NULL       },

    [TK_ARROW]   = { NULL,       infix_op   },

    [TK_LPAREN]  = { grouping,   NULL       },
    [TK_RPAREN]  = { NULL,       no_op      },

    [TK_NUMERAL] = { number,     NULL       },

    [TK_STRCONT] = { metastring, no_op      },
    [TK_STREND]  = { string,     no_op      },

    [TK_WORD]    = { NULL,       NULL       },
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

void prefix_op(Compiler *c)
{
  Token op_token = eat(c);
  UnaryOp op = prefix_ops[op_token.type];

  int r_bp = op.precedence;
  expr(c, r_bp); // Parse and emit right operand.

  // A little optimization; unary + does nothing.
  if (op_token.type != TK_PLUS)
    emit_byte(&c->code, op_token.line, op.opcode);
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

bool infix_op(Compiler *c, int min_bp)
{
  Token op_token = c->current;
  BinaryOp op = infix_ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  next(c); // Consume op_token

  int r_bp = l_bp + op.associativity;
  expr(c, r_bp); // Parse and emit right operand.

  emit_byte(&c->code, op_token.line, op.opcode);
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

bool postfix_op(Compiler *c, int min_bp)
{
  Token op_token = c->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  next(c); // Consume op_token

  emit_byte(&c->code, op_token.line, op.opcode);
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
bool led_op(Compiler *c, int min_bp)
{
  if (led_op_is_infix(c->current.type, peek(c).type))
    return infix_op(c, min_bp);
  else
    return postfix_op(c, min_bp);
}

// Comparison operators that can be chained.
// a < b <= c != 0
bool cmp_op(Compiler *c, int min_bp)
{
  if (PREC_CMP < min_bp)
    return true;

  Token op_token = eat(c);
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
  expr(c, r_bp); // Parse and emit right operand.

  // Allow chaining.
  if (is_cmp_token(c->current.type)) {
    // Previous op's rhs becomes next op's lhs!
    emit_byte(&c->code, line, OP_CHAIN_BINOP);

    emit_byte(&c->code, line, opcode);

    cmp_op(c, 0); // Parse and emit chaining operator.

    // 1 = 2 = 3
    // 1 = 2 AND 2 = 3
    emit_byte(&c->code, line, OP_AND);
  }

  else emit_byte(&c->code, line, opcode);

  return false;
}

void grouping(Compiler *c)
{
  next(c); // (

  // Parse (...)
  expr(c, PREC_NONE);

  if (!match(c, TK_RPAREN)) // )
    invalid_token(c->current);
}

void boolean(Compiler *c)
{
  Token tok = eat(c);
  bool P;
  switch (tok.type) {
  case TK_TRUE: P = true; break;
  case TK_FALSE: P = false; break;
  default: abort(); // Unreachable
  }
  emit_constant(&c->code, tok.line, value_new((int)P, boolean));
}

void number(Compiler *c)
{
  Token tok = eat(c);
  Str n_str = str_copy_slice(tok.raw_str);
  float64_t n = strtod(n_str.s, NULL);
  emit_constant(&c->code, tok.line, value_new(n, number));
}

void metastring(Compiler *c)
{
  string(c); // Consume STRCONT

  for (bool found_end = false; !found_end;) {
    switch (c->current.type) {
    case TK_STRCONT:
      string(c);
      emit_byte(&c->code, c->current.line, OP_CONCAT);
      break;
    case TK_STREND:
      found_end = true;
      break;
    default:
      expr(c, PREC_NONE); // \(...)
      emit_bytes(&c->code, c->current.line, 2, OP_TO_STR, OP_CONCAT);
    }
  }

  string(c);
  emit_byte(&c->code, c->current.line, OP_CONCAT);
}

void string(Compiler *c)
{
  Token tok = eat(c);
  Str str = str_copy_slice(tok.raw_str);
  emit_constant(&c->code, tok.line, value_new(str, string));
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
void expr(Compiler *c, int min_bp)
{
  Token lhs_token = c->current;
  const NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL) {
    error_out("Expecting expression.\n");
    invalid_token(lhs_token);
  }

  lhs_rule(c);

  for (;;) {
    Token op_token = c->current;
    const LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      error_out("Expecting operator.\n");
      invalid_token(op_token);
    }

    bool is_no_op = op_rule(c, min_bp);
    if (is_no_op)
      break; // Precedence is too small or op otherwise cannot be used as a LED
  }
}
