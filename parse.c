#include "lex.h"
#include "parse.h"
#include "val.h"

static inline Token peek(Parse *p)
{
  return p->lookahead;
}

static inline Token next(Parse *p)
{
  Token next_tok = p->lookahead;
  p->current = next_tok;
  p->lookahead = lex_token(&p->lex);
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

static Token consume(Parse *p, TokenType expected)
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
/*  token type         NUD         LED        */
    [TK_EOF]       = { NULL,       no_op      },
    [TK_ERR]       = { NULL,       NULL       },

    [TK_PLUS]      = { prefix_op,  infix_op   },
    [TK_MINUS]     = { prefix_op,  infix_op   },
    [TK_STAR]      = { NULL,       infix_op   },
    [TK_SLASH]     = { NULL,       infix_op   },
    [TK_CARET]     = { NULL,       infix_op   },
    [TK_PERCENT]   = { NULL,       led_op     },
    [TK_BANG]      = { NULL,       postfix_op },
    [TK_2PIPE]     = { NULL,       infix_op   },

    [TK_EQ]        = { NULL,       cmp_op     },
    [TK_NEQ]       = { NULL,       cmp_op     },
    [TK_LT]        = { NULL,       cmp_op     },
    [TK_GT]        = { NULL,       cmp_op     },
    [TK_LEQ]       = { NULL,       cmp_op     },
    [TK_GEQ]       = { NULL,       cmp_op     },

    [TK_ASSIGN]    = { NULL,       assignage  },
    [TK_2PLUS]     = { NULL,       NULL       },
    [TK_2MINUS]    = { NULL,       NULL       },
    [TK_LET]       = { let,        NULL       },

    [TK_NOT]       = { prefix_op,  NULL       },
    [TK_AND]       = { NULL,       infix_op   },
    [TK_OR]        = { NULL,       infix_op   },

    [TK_IF]        = { NULL,       NULL       },
    [TK_ELSE]      = { NULL,       NULL       },

    [TK_LOOP]      = { NULL,       NULL       },
    [TK_FOR]       = { NULL,       NULL       },
    [TK_WHILE]     = { NULL,       NULL       },
    [TK_BREAK]     = { NULL,       NULL       },
    [TK_CONTINUE]  = { NULL,       NULL       },

    [TK_TRUE]      = { boolean,    NULL       },
    [TK_FALSE]     = { boolean,    NULL       },

    [TK_ARROW]     = { NULL,       infix_op   },

    [TK_LPAREN]    = { grouping,   NULL       },
    [TK_RPAREN]    = { NULL,       no_op      },

    [TK_LCURLY]    = { block,      NULL       },
    [TK_RCURLY]    = { NULL,       no_op      },

    [TK_LBRACK]    = { list,       subscript  },
    [TK_RBRACK]    = { NULL,       no_op      },

    [TK_COLON]     = { NULL,       no_op      },
    [TK_SEMICOLON] = { NULL,       no_op      },
    [TK_COMMA]     = { NULL,       no_op      },

    [TK_NUMERAL]   = { number,     NULL       },

    [TK_STRCONT]   = { metastring, no_op      },
    [TK_STREND]    = { string,     no_op      },

    [TK_WORD]      = { ident,      NULL       },
  };

static inline
const ParseRule *parse_rule(TokenType type)
{
  return &parse_rules[type];
}

typedef struct {
  UnOp type;
  Precedence precedence;
} UnaryOp;

typedef struct {
  BinOp type;
  Precedence precedence;
  Associativity associativity;
} BinaryOp;

static const UnaryOp prefix_ops[] = {
  [TK_PLUS]  = { OP_POSITE, PREC_SIGN },
  [TK_MINUS] = { OP_NEGATE, PREC_SIGN },
  [TK_NOT] =   { OP_NOT,    PREC_NOT  },
};

TNode *prefix_op(Parse *p)
{
  Token op_token = eat(p);
  UnaryOp op = prefix_ops[op_token.type];

  // Parse right operand.
  int r_bp = (int)op.precedence;
  TNode *rhs = expr(p, r_bp);

  return treenode_op(AST_UNOP, op.type, op_token.line, 1, &rhs);
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

TNode *infix_op(Parse *p, TNode *lhs, int min_bp)
{
  Token op_token = p->current;
  BinaryOp op = infix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token

  // Parse right operand.
  int r_bp = (int)l_bp + (int)op.associativity;
  TNode *rhs = expr(p, r_bp);

  TNode *operands[2] = {lhs, rhs};
  return treenode_op(AST_BINOP, op.type, op_token.line, 2, operands);
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

TNode *postfix_op(Parse *p, TNode *lhs, int min_bp)
{
  Token op_token = p->current;
  UnaryOp op = postfix_ops[op_token.type];

  int l_bp = (int)op.precedence;
  if (l_bp < min_bp)
    return NULL;

  next(p); // Consume op_token

  return treenode_op(AST_UNOP, op.type, op_token.line, 1, &lhs);
}

static bool led_op_is_infix(TokenType op, TokenType next)
{
  const ParseRule *next_rule = parse_rule(next);

  if (next_rule->nud == NULL)
    return false; // Next token is not a valid rhs.

  else if (next_rule->led == NULL || !is_prefix_and_infix(next))
    return true; // Next token is not an infix op.

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
TNode *led_op(Parse *p, TNode *lhs, int min_bp)
{
  if (led_op_is_infix(p->current.type, peek(p).type))
    return infix_op(p, lhs, min_bp);
  else
    return postfix_op(p, lhs, min_bp);
}

static const BinOp cmp_types[] = {
  [TK_EQ]  = OP_EQ,
  [TK_NEQ] = OP_NEQ,
  [TK_LT]  = OP_LT,
  [TK_LEQ] = OP_LEQ,
  [TK_GT]  = OP_GT,
  [TK_GEQ] = OP_GEQ,
};

static TNode *cmp_op_chain(Parse *p, TNode *lhs)
{
  if (is_cmp_token(p->current.type)) {
    Token op_token = eat(p);
    TNode *cmp = treenode_new(AST_CONJUNCT_CMP, op_token.line);

    cmp->op.op_type = (Op)cmp_types[op_token.type];
    cmp->op.operands[0] = lhs;

    return cmp_op_chain(p, cmp);
  }
  else {
    const int r_bp = (int)PREC_CMP + (int)ASSOC_LEFT;
    return expr(p, r_bp);
  }
}

// Comparison operators that can be chained.
// a < b <= c != 0
TNode *cmp_op(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_CMP < min_bp)
    return NULL;

  return cmp_op_chain(p, lhs);
}

TNode *grouping(Parse *p)
{
  size_t line = next(p).line; // (

  // Parse (...)
  TNode *grouping = treenode_new(AST_GROUPING, line);
  grouping->expr = expr(p, PREC_NONE);

  if (!match(p, TK_RPAREN)) // )
    invalid_token(p->current);

  return grouping;
}

TNode *stmt(Parse *p)
{
  TokenType tok = p->current.type;

  if (parse_rule(tok)->nud == NULL)
    return NULL;
  else
    return expr(p, PREC_NONE);
}

// A block is a series of statements.
TNode *block(Parse *p)
{
  size_t line = eat(p).line; // {

  // Consume first statement
  TNode *first_stmt = stmt(p);

  if (!first_stmt)
    runtime_error("illegal empty block, expect statement");

  NodeList stmts = NodeList_with_cap(1);
  NodeList_push(&stmts, first_stmt);

  // Consume statements ...;
  while (match(p, TK_SEMICOLON)) {
    if (p->current.type == TK_RCURLY) {
      // Trailing semicolon, no value from block expr.
      NodeList_push(&stmts, treenode_new(AST_NONE, p->current.line));
      break;
    }

    TNode *s = stmt(p);
    if (s == NULL)
      break;

    NodeList_push(&stmts, s);
  }

  if (!match(p, TK_RCURLY)) // }
    invalid_token(p->current);

  return treenode_list(AST_BLOCK, line, stmts);
}

TNode *list(Parse *p)
{
  size_t line = next(p).line; // [
  NodeList elems = NodeList_new();

  // Consume list elements ...,
  // Allows a trailing comma.
  do {
    if (parse_rule(p->current.type)->nud == NULL)
      break;

    NodeList_push(&elems, expr(p, PREC_NONE));
  } while (match(p, TK_COMMA));

  if (!match(p, TK_RBRACK)) // ]
    invalid_token(p->current);

  return treenode_list(AST_LIST, line, elems);
}

TNode *boolean(Parse *p)
{
  Token booltok = eat(p);
  int P;
  switch (booltok.type) {
  case TK_TRUE: P = true; break;
  case TK_FALSE: P = false; break;
  default: abort(); // Unreachable
  }

  return treenode_constant(value_new(P, boolean), booltok.line);
}

TNode *number(Parse *p)
{
  Token ntok = eat(p);

  // Copying the slice to NUL-terminated so strtod doesn't parse anything extra
  Str nstr = str_from_slice(ntok.slice);
  float64_t n = strtod(nstr.s, NULL);
  free((void *)nstr.s);

  return treenode_constant(value_new(n, number), ntok.line);
}

TNode *metastring(Parse *p)
{
  NodeList metas = NodeList_new();
  size_t line = p->current.line;

  for (bool found_end = false; !found_end;) {
    switch (p->current.type) {
    case TK_STREND:
      found_end = true;
    case TK_STRCONT:
      if (p->current.slice.len == 0)
        next(p); // Skip empty string tokens.
      else {
        TNode *s = string(p);
        NodeList_push(&metas, s);
      }
      break;
    default:
      {
        TNode *interpd_expr = expr(p, PREC_NONE); // \(...)
        NodeList_push(&metas, interpd_expr);
      }
    }
  }

  return treenode_list(AST_METASTRING, line, metas);
}

TNode *string(Parse *p)
{
  Token strtok = eat(p);
  Str str = str_from_slice(strtok.slice);
  return treenode_constant(string_value_new(str), strtok.line);
}

// x
TNode *ident(Parse *p)
{
  Token ident_tok = eat(p);
  TNode *ident_node = treenode_new(AST_IDENT, ident_tok.line);
  ident_node->ident = ident_tok.slice;
  return ident_node;
}

TNode *subscript(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_SUBSCRIPT < min_bp)
    return NULL;

  size_t line = eat(p).line; // [
  TNode *idx = expr(p, PREC_NONE);
  consume(p, TK_RBRACK); // ]

  TNode *operands[2] = {lhs, idx};
  return treenode_op(AST_SUBSCRIPT, OP_NONE, line, 2, operands);
}

TNode *assignage(Parse *p, TNode *lhs, int min_bp)
{
  if (PREC_ASSIGN < min_bp)
    return NULL;

  size_t line = eat(p).line; // Consume :=

  // Parse assignment value.
  const int r_bp = (int)PREC_ASSIGN + (int)ASSOC_RIGHT;
  TNode *rhs = expr(p, r_bp);

  TNode *operands[2] = {lhs, rhs};
  return treenode_op(AST_ASSIGN, OP_NONE, line, 2, operands);
}

TNode *let(Parse *p)
{
  next(p); // let
  Token ident_tok = consume(p, TK_WORD);

  TNode *node = treenode_new(AST_LET, ident_tok.line);
  node->ident = ident_tok.slice;
  return node;
}

// An impl of Pratt parsing.
// Handles prefix, infix, postfix and mixfix expressions
TNode *expr(Parse *p, int min_bp)
{
  Token lhs_token = p->current;
  NudRule lhs_rule = parse_rule(lhs_token.type)->nud;

  if (lhs_rule == NULL) {
    error_out("Expecting expression.\n");
    invalid_token(lhs_token);
  }

  TNode *lhs = lhs_rule(p);

  for (;;) {
    Token op_token = p->current;
    LedRule op_rule = parse_rule(op_token.type)->led;

    if (op_rule == NULL) {
      error_out("Expecting operator.\n");
      invalid_token(op_token);
    }

    TNode *op_result = op_rule(p, lhs, min_bp);
    if (op_result == NULL)
      break; // Precedence is too small or op otherwise cannot be used as a LED

    lhs = op_result;
  }

  return lhs;
}
