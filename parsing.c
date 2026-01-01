#include "compiler.h"
#include "lex.h"
#include "parsing.h"

static inline Token peek(Compiler *c)
{
  return c->lookahead;
}

static Token next(Compiler *c)
{
  Token t = c->lookahead;
  c->current = t;

  if (t.type != TK_EOF)
    c->lookahead = lex_token(&c->lex);

  return t;
}

static Token eat(Compiler *c)
{
  Token t = c->current;
  next(c);
  return t;
}

static bool match(Compiler *c, TokenType expected)
{
  Token t = peek(c);
  if (t.type != TK_EOF && t.type == expected)
  {
    next(c);
    return true;
  }
  else return false;
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

void prefix_op(Compiler *c)
{
  Token op_token = eat(c);

  const UnaryOp ops[] = {
    [TK_PLUS] =  { OP_NONE,   PREC_SIGN },
    [TK_MINUS] = { OP_NEGATE, PREC_SIGN },
  };
  UnaryOp op = ops[op_token.type];

  int r_bp = op.precedence;
  expr(c, r_bp); // Parse and emit right operand.

  emit_byte(&c->code, op_token.line, op.opcode);
}

bool infix_op(Compiler *c, int min_bp)
{
  Token op_token = eat(c);

  const BinaryOp ops[] = {
    [TK_PLUS] =    { OP_ADD,    PREC_TERM,   ASSOC_LEFT  },
    [TK_MINUS] =   { OP_SUB,    PREC_TERM,   ASSOC_LEFT  },
    [TK_STAR] =    { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
    [TK_SLASH] =   { OP_MUL,    PREC_FACTOR, ASSOC_LEFT  },
    [TK_CARET] =   { OP_POW,    PREC_POWER,  ASSOC_RIGHT },
    [TK_PERCENT] = { OP_MODULO, PREC_FACTOR, ASSOC_LEFT  },
    [TK_ARROW] =   { OP_I9N,    PREC_I9N,    ASSOC_LEFT  },
  };
  BinaryOp op = ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  int r_bp = l_bp + op.associativity;
  expr(c, r_bp); // Parse and emit right operand.

  emit_byte(&c->code, op_token.line, op.opcode);
  return false;
}

bool postfix_op(Compiler *c, int min_bp)
{
  Token op_token = eat(c);

  const UnaryOp ops[] = {
    [TK_PERCENT] = { OP_PERCENTAGE, PREC_PERCENT   },
    [TK_BANG] =    { OP_FACTORIAL,  PREC_FACTORIAL },
  };
  UnaryOp op = ops[op_token.type];

  int l_bp = op.precedence;
  if (l_bp < min_bp)
    return true;

  emit_byte(&c->code, op_token.line, op.opcode);
  return false;
}

// For LED op tokens of ambiguous fixity
bool led_op(Compiler *c, int min_bp)
{
  if (parse_rule(peek(c).type)->nud == NULL)
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

  emit_byte(&c->code, op_token.line, opcode);

  // Allow chaining.
  if (is_cmp_token(c->current.type)) {
    // Previous op's rhs becomes next op's lhs
    emit_byte(&c->code, op_token.line, OP_CMP_RHS);
    cmp_op(c, 0); // Parse and emit chaining operator.
    emit_byte(&c->code, op_token.line, OP_AND);
  }

  return false;
}

void grouping(Compiler *c)
{
  next(c);

  // Parse (...)
  expr(c, PREC_NONE);

  if (!match(c, TK_RPAREN))
    invalid_token(c->current);
}

void number(Compiler *c)
{
  Value val = {VAL_NUMBER, atof(c->current.string.s)};
  size_t line = c->current.line;
  emit_constant(&c->code, line, val);
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
void expr(Compiler *c, int min_bp)
{
  Token lhs_token = eat(c);
  const NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL)
    invalid_token(lhs_token);

  for (;;) {
    Token op_token = eat(c);
    const LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL)
      invalid_token(op_token);

    bool is_no_op = op_rule(c, min_bp);
    if (is_no_op)
      break; // Precedence is too small or op otherwise cannot be used as a LED
  }
}
